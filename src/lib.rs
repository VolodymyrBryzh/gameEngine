use winit::{
    event::*,
    event_loop::{EventLoop},
    window::WindowBuilder,
    keyboard::{KeyCode, PhysicalKey},
};
use std::collections::HashSet;
use glam::{Vec3, Mat4};
use wgpu::util::DeviceExt;
use rapier3d::prelude::*;
use noise::{NoiseFn, Perlin};

// --- Графічні типи ---

#[repr(C)]
#[derive(Copy, Clone, Debug, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Vertex {
    position: [f32; 3],
    color: [f32; 3],
}

impl Vertex {
    fn desc() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<Vertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: std::mem::size_of::<[f32; 3]>() as wgpu::BufferAddress,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x3,
                },
            ],
        }
    }
}

// --- Камера ---

pub struct Camera {
    pub eye: Vec3,
    pub target: Vec3,
    pub up: Vec3,
    pub aspect: f32,
    pub fovy: f32,
    pub znear: f32,
    pub zfar: f32,
}

impl Camera {
    fn build_view_projection_matrix(&self) -> Mat4 {
        let view = Mat4::look_at_rh(self.eye, self.target, self.up);
        let proj = Mat4::perspective_rh(self.fovy.to_radians(), self.aspect, self.znear, self.zfar);
        proj * view
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct CameraUniform {
    view_proj: [[f32; 4]; 4],
}

impl CameraUniform {
    fn new() -> Self {
        Self {
            view_proj: Mat4::IDENTITY.to_cols_array_2d(),
        }
    }

    fn update_view_proj(&mut self, camera: &Camera) {
        self.view_proj = camera.build_view_projection_matrix().to_cols_array_2d();
    }
}

// --- Фізика ---

pub struct PhysicsContext {
    pub rigid_body_set: RigidBodySet,
    pub collider_set: ColliderSet,
    pub integration_parameters: IntegrationParameters,
    pub physics_pipeline: PhysicsPipeline,
    pub island_manager: IslandManager,
    pub broad_phase: BroadPhase,
    pub narrow_phase: NarrowPhase,
    pub impulse_joint_set: ImpulseJointSet,
    pub multibody_joint_set: MultibodyJointSet,
    pub ccd_solver: CCDSolver,
    pub gravity: Vector<f32>,
}

impl PhysicsContext {
    pub fn new() -> Self {
        Self {
            rigid_body_set: RigidBodySet::new(),
            collider_set: ColliderSet::new(),
            integration_parameters: IntegrationParameters::default(),
            physics_pipeline: PhysicsPipeline::new(),
            island_manager: IslandManager::new(),
            broad_phase: BroadPhase::new(),
            narrow_phase: NarrowPhase::new(),
            impulse_joint_set: ImpulseJointSet::new(),
            multibody_joint_set: MultibodyJointSet::new(),
            ccd_solver: CCDSolver::new(),
            gravity: vector![0.0, -9.81, 0.0],
        }
    }

    pub fn step(&mut self) {
        self.physics_pipeline.step(
            &self.gravity,
            &self.integration_parameters,
            &mut self.island_manager,
            &mut self.broad_phase,
            &mut self.narrow_phase,
            &mut self.rigid_body_set,
            &mut self.collider_set,
            &mut self.impulse_joint_set,
            &mut self.multibody_joint_set,
            &mut self.ccd_solver,
            None,
            &(),
            &(),
        );
    }
}

// --- Рушій ---

pub struct Engine {
    pub window: &'static winit::window::Window,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pub size: winit::dpi::PhysicalSize<u32>,

    render_pipeline: wgpu::RenderPipeline,
    
    // Куб (Гравець)
    cube_vertex_buffer: wgpu::Buffer,
    cube_index_buffer: wgpu::Buffer,
    num_cube_indices: u32,

    // Земля (Terrain)
    terrain_vertex_buffer: wgpu::Buffer,
    terrain_index_buffer: wgpu::Buffer,
    num_terrain_indices: u32,

    pub camera: Camera,
    camera_uniform: CameraUniform,
    camera_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,

    pub world: hecs::World,
    pub input: HashSet<KeyCode>,
    pub physics: PhysicsContext,
}

impl Engine {
    pub async fn new(window: winit::window::Window) -> Self {
        let size = window.inner_size();
        let window: &'static winit::window::Window = Box::leak(Box::new(window));

        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });

        let surface = instance.create_surface(window).unwrap();

        let adapter = instance.request_adapter(&wgpu::RequestAdapterOptions {
            compatible_surface: Some(&surface),
            ..Default::default()
        }).await.unwrap();

        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor::default(), None).await.unwrap();

        let caps = surface.get_capabilities(&adapter);
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: caps.formats[0],
            width: size.width,
            height: size.height,
            present_mode: caps.present_modes[0],
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        // --- Камера ---
        let camera = Camera {
            eye: (0.0, 20.0, 30.0).into(),
            target: (0.0, 0.0, 0.0).into(),
            up: Vec3::Y,
            aspect: config.width as f32 / config.height as f32,
            fovy: 45.0,
            znear: 0.1,
            zfar: 1000.0,
        };

        let mut camera_uniform = CameraUniform::new();
        camera_uniform.update_view_proj(&camera);

        let camera_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Camera Buffer"),
            contents: bytemuck::cast_slice(&[camera_uniform]),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });

        let camera_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
            label: None,
        });

        let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            layout: &camera_bind_group_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: camera_buffer.as_entire_binding(),
            }],
            label: None,
        });

        // --- Pipeline ---
        let shader = device.create_shader_module(wgpu::include_wgsl!("shader.wgsl"));
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            bind_group_layouts: &[&camera_bind_group_layout],
            ..Default::default()
        });

        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Render Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: "vs_main",
                buffers: &[Vertex::desc()],
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: "fs_main",
                targets: &[Some(wgpu::ColorTargetState {
                    format: config.format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            primitive: wgpu::PrimitiveState {
                cull_mode: Some(wgpu::Face::Back),
                ..Default::default()
            },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
        });

        // --- Геометрія Куба (Гравець) ---
        let cube_vertices: &[Vertex] = &[
            Vertex { position: [-0.5, -0.5,  0.5], color: [1.0, 0.0, 0.0] },
            Vertex { position: [ 0.5, -0.5,  0.5], color: [0.0, 1.0, 0.0] },
            Vertex { position: [ 0.5,  0.5,  0.5], color: [0.0, 0.0, 1.0] },
            Vertex { position: [-0.5,  0.5,  0.5], color: [1.0, 1.0, 0.0] },
            Vertex { position: [-0.5, -0.5, -0.5], color: [1.0, 0.0, 1.0] },
            Vertex { position: [ 0.5, -0.5, -0.5], color: [0.0, 1.0, 1.0] },
            Vertex { position: [ 0.5,  0.5, -0.5], color: [1.0, 1.0, 1.0] },
            Vertex { position: [-0.5,  0.5, -0.5], color: [0.5, 0.5, 0.5] },
        ];
        let cube_indices: &[u16] = &[
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
        ];

        let cube_vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Cube Vertex Buffer"),
            contents: bytemuck::cast_slice(cube_vertices),
            usage: wgpu::BufferUsages::VERTEX,
        });
        let cube_index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Cube Index Buffer"),
            contents: bytemuck::cast_slice(cube_indices),
            usage: wgpu::BufferUsages::INDEX,
        });

        // --- Генерація Землі (Terrain) ---
        let mut terrain_vertices = Vec::new();
        let mut terrain_indices = Vec::new();
        let terrain_size = 100;
        let terrain_res = 100;
        let perlin = Perlin::new(1);
        
        let mut heights = DMatrix::zeros(terrain_res, terrain_res);

        for z in 0..terrain_res {
            for x in 0..terrain_res {
                let fx = x as f32 / terrain_res as f32 * terrain_size as f32 - terrain_size as f32 / 2.0;
                let fz = z as f32 / terrain_res as f32 * terrain_size as f32 - terrain_size as f32 / 2.0;
                
                // Генеруємо висоту через шум
                let h = perlin.get([fx as f64 * 0.1, fz as f64 * 0.1]) as f32 * 3.0;
                heights[(z, x)] = h;

                let color = if h > 1.0 { [0.5, 0.5, 0.5] } else { [0.2, 0.5, 0.2] };
                terrain_vertices.push(Vertex { position: [fx, h, fz], color });
            }
        }

        for z in 0..terrain_res - 1 {
            for x in 0..terrain_res - 1 {
                let i = (z * terrain_res + x) as u16;
                terrain_indices.extend_from_slice(&[i, i + terrain_res as u16, i + 1]);
                terrain_indices.extend_from_slice(&[i + 1, i + terrain_res as u16, i + terrain_res as u16 + 1]);
            }
        }

        let terrain_vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Terrain Vertex Buffer"),
            contents: bytemuck::cast_slice(&terrain_vertices),
            usage: wgpu::BufferUsages::VERTEX,
        });
        let terrain_index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Terrain Index Buffer"),
            contents: bytemuck::cast_slice(&terrain_indices),
            usage: wgpu::BufferUsages::INDEX,
        });

        // --- Фізика ---
        let mut physics = PhysicsContext::new();
        
        // Фізичний ландшафт
        let terrain_collider = ColliderBuilder::heightfield(heights, vector![terrain_size as f32, 1.0, terrain_size as f32])
            .build();
        physics.collider_set.insert(terrain_collider);

        // Гравець
        let rigid_body = RigidBodyBuilder::dynamic()
            .translation(vector![0.0, 10.0, 0.0])
            .lock_rotations()
            .build();
        let handle = physics.rigid_body_set.insert(rigid_body);
        let player_collider = ColliderBuilder::cuboid(0.5, 0.5, 0.5).build();
        physics.collider_set.insert_with_parent(player_collider, handle, &mut physics.rigid_body_set);

        let mut world = hecs::World::new();
        world.spawn((handle,));

        Self {
            window,
            surface,
            device,
            queue,
            config,
            size,
            render_pipeline,
            cube_vertex_buffer,
            cube_index_buffer,
            num_cube_indices: cube_indices.len() as u32,
            terrain_vertex_buffer,
            terrain_index_buffer,
            num_terrain_indices: terrain_indices.len() as u32,
            camera,
            camera_uniform,
            camera_buffer,
            camera_bind_group,
            world,
            input: HashSet::new(),
            physics,
        }
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            self.size = new_size;
            self.config.width = new_size.width;
            self.config.height = new_size.height;
            self.camera.aspect = self.config.width as f32 / self.config.height as f32;
            self.surface.configure(&self.device, &self.config);
        }
    }

    pub fn update(&mut self) {
        self.physics.step();

        let mut move_dir = Vec3::ZERO;
        if self.input.contains(&KeyCode::KeyW) { move_dir.z -= 1.0; }
        if self.input.contains(&KeyCode::KeyS) { move_dir.z += 1.0; }
        if self.input.contains(&KeyCode::KeyA) { move_dir.x -= 1.0; }
        if self.input.contains(&KeyCode::KeyD) { move_dir.x += 1.0; }

        for (_id, handle) in self.world.query_mut::<&rapier3d::dynamics::RigidBodyHandle>() {
            let body = self.physics.rigid_body_set.get_mut(*handle).unwrap();
            let pos = *body.translation(); // Копіюємо значення, щоб уникнути конфлікту запозичень
            
            if move_dir.length() > 0.0 {
                let move_dir = move_dir.normalize() * 0.1;
                body.set_translation(pos + vector![move_dir.x, 0.0, move_dir.z], true);
            }

            self.camera.target = Vec3::new(pos.x, pos.y, pos.z);
            self.camera.eye = self.camera.target + Vec3::new(0.0, 10.0, 20.0);
        }

        self.camera_uniform.update_view_proj(&self.camera);
        self.queue.write_buffer(&self.camera_buffer, 0, bytemuck::cast_slice(&[self.camera_uniform]));
    }

    pub fn render(&mut self) -> Result<(), wgpu::SurfaceError> {
        let output = self.surface.get_current_texture()?;
        let view = output.texture.create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());

        {
            let mut rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                ..Default::default()
            });

            rp.set_pipeline(&self.render_pipeline);
            rp.set_bind_group(0, &self.camera_bind_group, &[]);
            
            // Малюємо Землю
            rp.set_vertex_buffer(0, self.terrain_vertex_buffer.slice(..));
            rp.set_index_buffer(self.terrain_index_buffer.slice(..), wgpu::IndexFormat::Uint16);
            rp.draw_indexed(0..self.num_terrain_indices, 0, 0..1);

            // Малюємо Гравця
            rp.set_vertex_buffer(0, self.cube_vertex_buffer.slice(..));
            rp.set_index_buffer(self.cube_index_buffer.slice(..), wgpu::IndexFormat::Uint16);
            rp.draw_indexed(0..self.num_cube_indices, 0, 0..1);
        }

        self.queue.submit(std::iter::once(encoder.finish()));
        output.present();
        Ok(())
    }
}

pub async fn run() {
    env_logger::init();
    let event_loop = EventLoop::new().unwrap();
    let window = WindowBuilder::new().with_title("Survival Engine - Terrain").build(&event_loop).unwrap();
    let mut engine = Engine::new(window).await;

    let _ = event_loop.run(move |event, elwt| {
        match event {
            Event::WindowEvent { ref event, .. } => match event {
                WindowEvent::CloseRequested => elwt.exit(),
                WindowEvent::Resized(s) => engine.resize(*s),
                WindowEvent::KeyboardInput { event, .. } => {
                    if let PhysicalKey::Code(key) = event.physical_key {
                        if event.state == ElementState::Pressed { engine.input.insert(key); }
                        else { engine.input.remove(&key); }
                    }
                }
                WindowEvent::RedrawRequested => {
                    engine.update();
                    let _ = engine.render();
                }
                _ => {}
            },
            Event::AboutToWait => engine.window.request_redraw(),
            _ => {}
        }
    });
}
