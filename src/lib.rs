use winit::{
    event::*,
    event_loop::{EventLoop},
    window::WindowBuilder,
    keyboard::{KeyCode, PhysicalKey},
};
use std::collections::HashSet;
use glam::{Vec3, Mat4, Quat};
use wgpu::util::DeviceExt;
use rapier3d::prelude::*;
use noise::{NoiseFn, Perlin};
use instant::Instant;

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
            array_stride: 24,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute { offset: 0, shader_location: 0, format: wgpu::VertexFormat::Float32x3 },
                wgpu::VertexAttribute { offset: 12, shader_location: 1, format: wgpu::VertexFormat::Float32x3 },
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
    pub yaw: f32,
    pub pitch: f32,
    pub distance: f32,
}

impl Camera {
    fn update_position(&mut self, target: Vec3) {
        self.target = target;
        let p_rad = self.pitch.to_radians();
        let y_rad = self.yaw.to_radians();
        let h_dist = self.distance * p_rad.cos();
        let v_dist = self.distance * p_rad.sin();
        self.eye = Vec3::new(
            target.x - h_dist * y_rad.cos(),
            target.y - h_dist * y_rad.sin(),
            target.z + v_dist
        );
    }
    fn build_view_projection_matrix(&self) -> Mat4 {
        let view = Mat4::look_at_rh(self.eye, self.target, self.up);
        let proj = Mat4::perspective_rh(self.fovy.to_radians(), self.aspect, self.znear, self.zfar);
        proj * view
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct CameraUniform { view_proj: [[f32; 4]; 4] }

impl CameraUniform {
    fn new() -> Self { Self { view_proj: Mat4::IDENTITY.to_cols_array_2d() } }
    fn update_view_proj(&mut self, camera: &Camera) { self.view_proj = camera.build_view_projection_matrix().to_cols_array_2d(); }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct ModelUniform { model_matrix: [[f32; 4]; 4] }

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
            gravity: vector![0.0, 0.0, -15.0],
        }
    }
    pub fn step(&mut self) {
        self.physics_pipeline.step(
            &self.gravity, &self.integration_parameters, &mut self.island_manager,
            &mut self.broad_phase, &mut self.narrow_phase, &mut self.rigid_body_set,
            &mut self.collider_set, &mut self.impulse_joint_set, &mut self.multibody_joint_set,
            &mut self.ccd_solver, None, &(), &(),
        );
    }
}

pub struct Player;
pub struct PhysicsHandle(pub rapier3d::dynamics::RigidBodyHandle);

pub struct Engine {
    pub window: &'static winit::window::Window,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pub size: winit::dpi::PhysicalSize<u32>,
    render_pipeline: wgpu::RenderPipeline,
    cube_vertex_buffer: wgpu::Buffer,
    cube_index_buffer: wgpu::Buffer,
    num_cube_indices: u32,
    terrain_vertex_buffer: wgpu::Buffer,
    terrain_index_buffer: wgpu::Buffer,
    num_terrain_indices: u32,
    pub camera: Camera,
    camera_uniform: CameraUniform,
    camera_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,
    model_buffer: wgpu::Buffer,
    model_bind_group: wgpu::BindGroup,
    pub world: hecs::World,
    pub input: HashSet<KeyCode>,
    pub mouse_delta: (f32, f32),
    pub physics: PhysicsContext,
    last_frame_inst: Instant,
}

impl Engine {
    pub async fn new(window: winit::window::Window) -> Self {
        let size = window.inner_size();
        let window: &'static winit::window::Window = Box::leak(Box::new(window));
        window.set_cursor_grab(winit::window::CursorGrabMode::Confined).ok();
        window.set_cursor_visible(false);

        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::default());
        let surface = instance.create_surface(window).unwrap();
        let adapter = instance.request_adapter(&wgpu::RequestAdapterOptions { compatible_surface: Some(&surface), ..Default::default() }).await.unwrap();
        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor::default(), None).await.unwrap();
        let caps = surface.get_capabilities(&adapter);
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: caps.formats[0],
            width: size.width, height: size.height,
            present_mode: caps.present_modes[0],
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        let camera = Camera {
            eye: (0.0, -30.0, 30.0).into(), target: (0.0, 0.0, 0.0).into(), up: Vec3::Z,
            aspect: config.width as f32 / config.height as f32, fovy: 45.0, znear: 0.1, zfar: 2000.0,
            yaw: 90.0, pitch: 30.0, distance: 40.0,
        };
        let mut camera_uniform = CameraUniform::new();
        camera_uniform.update_view_proj(&camera);
        let camera_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&[camera_uniform]), usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST });
        let camera_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            entries: &[wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }],
            label: None,
        });
        let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor { layout: &camera_bind_group_layout, entries: &[wgpu::BindGroupEntry { binding: 0, resource: camera_buffer.as_entire_binding() }], label: None });

        let model_uniform = ModelUniform { model_matrix: Mat4::IDENTITY.to_cols_array_2d() };
        let model_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&[model_uniform]), usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST });
        let model_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            entries: &[wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }],
            label: None,
        });
        let model_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor { layout: &model_bind_group_layout, entries: &[wgpu::BindGroupEntry { binding: 0, resource: model_buffer.as_entire_binding() }], label: None });

        let shader = device.create_shader_module(wgpu::include_wgsl!("shader.wgsl"));
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { bind_group_layouts: &[&camera_bind_group_layout, &model_bind_group_layout], ..Default::default() });
        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: None, layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: &[Vertex::desc()] },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: "fs_main", targets: &[Some(wgpu::ColorTargetState { format: config.format, blend: Some(wgpu::BlendState::REPLACE), write_mask: wgpu::ColorWrites::ALL })] }),
            primitive: wgpu::PrimitiveState { cull_mode: Some(wgpu::Face::Back), ..Default::default() },
            depth_stencil: None, multisample: wgpu::MultisampleState::default(), multiview: None,
        });

        let cube_vertices: &[Vertex] = &[
            Vertex { position: [-0.5, -0.5,  0.5], color: [1.0, 0.2, 0.2] }, Vertex { position: [ 0.5, -0.5,  0.5], color: [0.2, 1.0, 0.2] },
            Vertex { position: [ 0.5,  0.5,  0.5], color: [0.2, 0.2, 1.0] }, Vertex { position: [-0.5,  0.5,  0.5], color: [1.0, 1.0, 0.2] },
            Vertex { position: [-0.5, -0.5, -0.5], color: [1.0, 0.2, 1.0] }, Vertex { position: [ 0.5, -0.5, -0.5], color: [0.2, 1.0, 1.0] },
            Vertex { position: [ 0.5,  0.5, -0.5], color: [1.0, 1.0, 1.0] }, Vertex { position: [-0.5,  0.5, -0.5], color: [0.5, 0.5, 0.5] },
        ];
        let cube_indices: &[u16] = &[0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4];
        let cube_vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(cube_vertices), usage: wgpu::BufferUsages::VERTEX });
        let cube_index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(cube_indices), usage: wgpu::BufferUsages::INDEX });

        let mut terrain_vertices = Vec::new();
        let mut terrain_indices = Vec::new();
        let terrain_size = 600;
        let terrain_res = 100;
        let perlin = Perlin::new(123);
        let mut heights = DMatrix::zeros(terrain_res, terrain_res);
        for y in 0..terrain_res {
            for x in 0..terrain_res {
                let fx = (x as f32 / terrain_res as f32) * terrain_size as f32 - terrain_size as f32 / 2.0;
                let fy = (y as f32 / terrain_res as f32) * terrain_size as f32 - terrain_size as f32 / 2.0;
                let h = perlin.get([fx as f64 * 0.015, fy as f64 * 0.015]) as f32 * 12.0;
                heights[(y, x)] = h;
                let color = if h > 6.0 { [0.9, 0.9, 0.9] } else if h > 2.0 { [0.5, 0.5, 0.5] } else { [0.2, 0.5, 0.2] };
                terrain_vertices.push(Vertex { position: [fx, fy, h], color });
            }
        }
        for y in 0..terrain_res - 1 {
            for x in 0..terrain_res - 1 {
                let i = (y * terrain_res + x) as u16;
                terrain_indices.extend_from_slice(&[i, i + terrain_res as u16, i + 1]);
                terrain_indices.extend_from_slice(&[i + 1, i + terrain_res as u16, i + terrain_res as u16 + 1]);
            }
        }
        let terrain_vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&terrain_vertices), usage: wgpu::BufferUsages::VERTEX });
        let terrain_index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&terrain_indices), usage: wgpu::BufferUsages::INDEX });

        let mut physics = PhysicsContext::new();
        // ВАЖЛИВО: Використовуємо правильну орієнтацію HeightField для Z-up
        let terrain_collider = ColliderBuilder::heightfield(heights, vector![terrain_size as f32, terrain_size as f32, 1.0])
            .rotation(vector![1.5707, 0.0, 0.0])
            .build();
        physics.collider_set.insert(terrain_collider);

        let rigid_body = RigidBodyBuilder::dynamic()
            .translation(vector![0.0, 0.0, 50.0])
            .lock_rotations()
            .linear_damping(1.0)
            .build();
        let handle = physics.rigid_body_set.insert(rigid_body);
        let player_collider = ColliderBuilder::cuboid(0.5, 0.5, 0.5).friction(0.0).build();
        physics.collider_set.insert_with_parent(player_collider, handle, &mut physics.rigid_body_set);

        let mut world = hecs::World::new();
        world.spawn((Player, PhysicsHandle(handle)));

        Self {
            window, surface, device, queue, config, size, render_pipeline, cube_vertex_buffer, cube_index_buffer, num_cube_indices: cube_indices.len() as u32,
            terrain_vertex_buffer, terrain_index_buffer, num_terrain_indices: terrain_indices.len() as u32,
            camera, camera_uniform, camera_buffer, camera_bind_group, model_buffer, model_bind_group,
            world, input: HashSet::new(), mouse_delta: (0.0, 0.0), physics, last_frame_inst: Instant::now(),
        }
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            self.size = new_size; self.config.width = new_size.width; self.config.height = new_size.height;
            self.camera.aspect = self.config.width as f32 / self.config.height as f32;
            self.surface.configure(&self.device, &self.config);
        }
    }

    pub fn update(&mut self) {
        let _dt = self.last_frame_inst.elapsed().as_secs_f32();
        self.last_frame_inst = Instant::now();
        
        // Примусово просуваємо фізику
        self.physics.step();

        self.camera.yaw += self.mouse_delta.0 * 0.15;
        self.camera.pitch = (self.camera.pitch - self.mouse_delta.1 * 0.15).clamp(5.0, 85.0);
        self.mouse_delta = (0.0, 0.0);

        let mut move_dir = Vec3::ZERO;
        let y_rad = self.camera.yaw.to_radians();
        let forward = Vec3::new(y_rad.cos(), y_rad.sin(), 0.0).normalize();
        let right = Vec3::new(y_rad.sin(), -y_rad.cos(), 0.0).normalize();

        if self.input.contains(&KeyCode::KeyW) { move_dir += forward; }
        if self.input.contains(&KeyCode::KeyS) { move_dir -= forward; }
        if self.input.contains(&KeyCode::KeyA) { move_dir += right; }
        if self.input.contains(&KeyCode::KeyD) { move_dir -= right; }

        for (_id, (handle, _player)) in self.world.query_mut::<(&PhysicsHandle, &Player)>() {
            let body = self.physics.rigid_body_set.get_mut(handle.0).unwrap();
            
            // ПРИМУСОВО БУДИМО ТІЛО КОЖЕН КАДР ДЛЯ ТЕСТУ
            body.wake_up(true);

            if move_dir.length() > 0.0 {
                let v = move_dir.normalize() * 30.0;
                body.set_linvel(vector![v.x, v.y, body.linvel().z], true);
            } else {
                body.set_linvel(vector![0.0, 0.0, body.linvel().z], true);
            }

            if self.input.contains(&KeyCode::Space) && body.linvel().z.abs() < 0.2 {
                body.apply_impulse(vector![0.0, 0.0, 20.0], true);
            }

            let pos = *body.translation();
            self.camera.update_position(Vec3::new(pos.x, pos.y, pos.z));
            self.window.set_title(&format!("Physics Active - Z: {:.2}", pos.z));
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
                    view: &view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }), store: wgpu::StoreOp::Store },
                })], ..Default::default()
            });
            rp.set_pipeline(&self.render_pipeline);
            rp.set_bind_group(0, &self.camera_bind_group, &[]);
            let terrain_model = ModelUniform { model_matrix: Mat4::IDENTITY.to_cols_array_2d() };
            self.queue.write_buffer(&self.model_buffer, 0, bytemuck::cast_slice(&[terrain_model]));
            rp.set_bind_group(1, &self.model_bind_group, &[]);
            rp.set_vertex_buffer(0, self.terrain_vertex_buffer.slice(..));
            rp.set_index_buffer(self.terrain_index_buffer.slice(..), wgpu::IndexFormat::Uint16);
            rp.draw_indexed(0..self.num_terrain_indices, 0, 0..1);
            for (_id, (handle, _player)) in self.world.query_mut::<(&PhysicsHandle, &Player)>() {
                let body = self.physics.rigid_body_set.get(handle.0).unwrap();
                let pos = body.translation();
                let model_mat = Mat4::from_translation(Vec3::new(pos.x, pos.y, pos.z));
                let player_model = ModelUniform { model_matrix: model_mat.to_cols_array_2d() };
                self.queue.write_buffer(&self.model_buffer, 0, bytemuck::cast_slice(&[player_model]));
                rp.set_bind_group(1, &self.model_bind_group, &[]);
                rp.set_vertex_buffer(0, self.cube_vertex_buffer.slice(..));
                rp.set_index_buffer(self.cube_index_buffer.slice(..), wgpu::IndexFormat::Uint16);
                rp.draw_indexed(0..self.num_cube_indices, 0, 0..1);
            }
        }
        self.queue.submit(std::iter::once(encoder.finish()));
        output.present(); Ok(())
    }
}

pub async fn run() {
    env_logger::init();
    let event_loop = EventLoop::new().unwrap();
    let window = WindowBuilder::new().with_title("Survival Engine").build(&event_loop).unwrap();
    let mut engine = Engine::new(window).await;
    let _ = event_loop.run(move |event, elwt| {
        match event {
            Event::DeviceEvent { event: DeviceEvent::MouseMotion { delta }, .. } => {
                engine.mouse_delta.0 += delta.0 as f32; engine.mouse_delta.1 += delta.1 as f32;
            }
            Event::WindowEvent { ref event, .. } => match event {
                WindowEvent::CloseRequested => elwt.exit(),
                WindowEvent::Resized(s) => engine.resize(*s),
                WindowEvent::KeyboardInput { event, .. } => {
                    if let PhysicalKey::Code(key) = event.physical_key {
                        if event.state == ElementState::Pressed { engine.input.insert(key); }
                        else { engine.input.remove(&key); }
                    }
                }
                WindowEvent::RedrawRequested => { engine.update(); let _ = engine.render(); }
                _ => {}
            },
            Event::AboutToWait => engine.window.request_redraw(),
            _ => {}
        }
    });
}
