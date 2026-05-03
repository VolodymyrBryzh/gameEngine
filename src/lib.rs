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

// --- Типи ---

#[repr(C)]
#[derive(Copy, Clone, Debug, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Vertex { position: [f32; 3], color: [f32; 3] }

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

pub struct Camera {
    pub eye: Vec3, pub target: Vec3, pub up: Vec3,
    pub aspect: f32, pub fovy: f32, pub znear: f32, pub zfar: f32,
    pub yaw: f32, pub pitch: f32, pub distance: f32,
}

impl Camera {
    fn update_position(&mut self, target: Vec3) {
        self.target = target;
        let p_rad = self.pitch.to_radians();
        let y_rad = self.yaw.to_radians();
        let h_dist = self.distance * p_rad.cos();
        let v_dist = self.distance * p_rad.sin();
        self.eye = Vec3::new(target.x - h_dist * y_rad.cos(), target.y - h_dist * y_rad.sin(), target.z + v_dist);
    }
    fn build_matrix(&self) -> Mat4 {
        Mat4::perspective_rh(self.fovy.to_radians(), self.aspect, self.znear, self.zfar) * Mat4::look_at_rh(self.eye, self.target, self.up)
    }
}

pub struct Engine {
    pub window: &'static winit::window::Window,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    render_pipeline: wgpu::RenderPipeline,
    cube_vb: wgpu::Buffer, cube_ib: wgpu::Buffer, num_cube_idx: u32,
    pub camera: Camera,
    camera_buf: wgpu::Buffer, camera_bg: wgpu::BindGroup,
    model_buf: wgpu::Buffer, model_bg: wgpu::BindGroup,
    pub input: HashSet<KeyCode>,
    pub mouse_delta: (f32, f32),
    pub rigid_body_set: RigidBodySet,
    pub player_handle: RigidBodyHandle,
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
        let config = surface.get_default_config(&adapter, size.width, size.height).unwrap();
        surface.configure(&device, &config);

        let camera = Camera {
            eye: Vec3::new(0.0, -30.0, 30.0), target: Vec3::ZERO, up: Vec3::Z,
            aspect: config.width as f32 / config.height as f32, fovy: 45.0, znear: 0.1, zfar: 1000.0,
            yaw: 90.0, pitch: 30.0, distance: 50.0,
        };
        let camera_buf = device.create_buffer(&wgpu::BufferDescriptor { label: None, size: 64, usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST, mapped_at_creation: false });
        let camera_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor { entries: &[wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }], label: None });
        let camera_bg = device.create_bind_group(&wgpu::BindGroupDescriptor { layout: &camera_bgl, entries: &[wgpu::BindGroupEntry { binding: 0, resource: camera_buf.as_entire_binding() }], label: None });
        let model_buf = device.create_buffer(&wgpu::BufferDescriptor { label: None, size: 64, usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST, mapped_at_creation: false });
        let model_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor { entries: &[wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }], label: None });
        let model_bg = device.create_bind_group(&wgpu::BindGroupDescriptor { layout: &model_bgl, entries: &[wgpu::BindGroupEntry { binding: 0, resource: model_buf.as_entire_binding() }], label: None });

        let shader = device.create_shader_module(wgpu::include_wgsl!("shader.wgsl"));
        let pll = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { bind_group_layouts: &[&camera_bgl, &model_bgl], ..Default::default() });
        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: None, layout: Some(&pll),
            vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: &[Vertex::desc()] },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: "fs_main", targets: &[Some(wgpu::ColorTargetState { format: config.format, blend: Some(wgpu::BlendState::REPLACE), write_mask: wgpu::ColorWrites::ALL })] }),
            primitive: wgpu::PrimitiveState { cull_mode: Some(wgpu::Face::Back), ..Default::default() },
            depth_stencil: None, multisample: wgpu::MultisampleState::default(), multiview: None,
        });

        let cube_verts = &[
            Vertex { position: [-0.5, -0.5,  0.5], color: [1.0, 0.0, 0.0] }, Vertex { position: [ 0.5, -0.5,  0.5], color: [0.0, 1.0, 0.0] },
            Vertex { position: [ 0.5,  0.5,  0.5], color: [0.0, 0.0, 1.0] }, Vertex { position: [-0.5,  0.5,  0.5], color: [1.0, 1.0, 0.0] },
            Vertex { position: [-0.5, -0.5, -0.5], color: [1.0, 0.0, 1.0] }, Vertex { position: [ 0.5, -0.5, -0.5], color: [0.0, 1.0, 1.0] },
            Vertex { position: [ 0.5,  0.5, -0.5], color: [1.0, 1.0, 1.0] }, Vertex { position: [-0.5,  0.5, -0.5], color: [0.5, 0.5, 0.5] },
        ];
        let cube_idx: &[u16] = &[0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4];
        let cube_vb = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(cube_verts), usage: wgpu::BufferUsages::VERTEX });
        let cube_ib = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(cube_idx), usage: wgpu::BufferUsages::INDEX });

        let mut rigid_body_set = RigidBodySet::new();
        let rb = RigidBodyBuilder::dynamic().translation(vector![0.0, 0.0, 40.0]).lock_rotations().build();
        let player_handle = rigid_body_set.insert(rb);

        Self {
            window, surface, device, queue, config, render_pipeline,
            cube_vb, cube_ib, num_cube_idx: cube_idx.len() as u32,
            camera, camera_buf, camera_bg, model_buf, model_bg,
            input: HashSet::new(), mouse_delta: (0.0, 0.0),
            rigid_body_set, player_handle,
        }
    }

    pub fn update(&mut self) {
        let dt = 1.0 / 60.0;
        let gravity = vector![0.0, 0.0, -9.81];

        self.camera.yaw += self.mouse_delta.0 * 0.15;
        self.camera.pitch = (self.camera.pitch - self.mouse_delta.1 * 0.15).clamp(5.0, 85.0);
        self.mouse_delta = (0.0, 0.0);

        let body = self.rigid_body_set.get_mut(self.player_handle).unwrap();
        let mut move_dir = Vec3::ZERO;
        let y_rad = self.camera.yaw.to_radians();
        let forward = Vec3::new(y_rad.cos(), y_rad.sin(), 0.0).normalize();
        let right = Vec3::new(y_rad.sin(), -y_rad.cos(), 0.0).normalize();

        if self.input.contains(&KeyCode::KeyW) { move_dir += forward; }
        if self.input.contains(&KeyCode::KeyS) { move_dir -= forward; }
        if self.input.contains(&KeyCode::KeyA) { move_dir += right; }
        if self.input.contains(&KeyCode::KeyD) { move_dir -= right; }

        let mut vel = *body.linvel();
        vel += gravity * dt;
        
        if move_dir.length() > 0.0 {
            let v = move_dir.normalize() * 20.0;
            vel.x = v.x; vel.y = v.y;
        } else {
            vel.x = 0.0; vel.y = 0.0;
        }

        if self.input.contains(&KeyCode::Space) && vel.z.abs() < 0.1 {
            vel.z = 15.0;
        }

        body.set_linvel(vel, true);
        
        // РУЧНА ІНТЕГРАЦІЯ ПОЗИЦІЇ
        let old_pos = *body.translation();
        let new_pos = old_pos + body.linvel() * dt;
        body.set_translation(new_pos, true);

        self.camera.update_position(Vec3::new(new_pos.x, new_pos.y, new_pos.z));
        self.window.set_title(&format!("Z: {:.2}", new_pos.z));

        let camera_matrix = self.camera.build_matrix();
        self.queue.write_buffer(&self.camera_buf, 0, bytemuck::cast_slice(&[camera_matrix.to_cols_array_2d()]));
    }

    pub fn render(&mut self) -> Result<(), wgpu::SurfaceError> {
        let output = self.surface.get_current_texture()?;
        let view = output.texture.create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        {
            let mut rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                color_attachments: &[Some(wgpu::RenderPassColorAttachment { view: &view, resolve_target: None, ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }), store: wgpu::StoreOp::Store } })], ..Default::default()
            });
            rp.set_pipeline(&self.render_pipeline);
            rp.set_bind_group(0, &self.camera_bg, &[]);
            let pos = self.rigid_body_set.get(self.player_handle).unwrap().translation();
            let m = Mat4::from_translation(Vec3::new(pos.x, pos.y, pos.z));
            self.queue.write_buffer(&self.model_buf, 0, bytemuck::cast_slice(&[m.to_cols_array_2d()]));
            rp.set_bind_group(1, &self.model_bg, &[]);
            rp.set_vertex_buffer(0, self.cube_vb.slice(..));
            rp.set_index_buffer(self.cube_ib.slice(..), wgpu::IndexFormat::Uint16);
            rp.draw_indexed(0..self.num_cube_idx, 0, 0..1);
        }
        self.queue.submit(std::iter::once(encoder.finish()));
        output.present(); Ok(())
    }
}

pub async fn run() {
    let event_loop = EventLoop::new().unwrap();
    let window = WindowBuilder::new().build(&event_loop).unwrap();
    let mut engine = Engine::new(window).await;
    let _ = event_loop.run(move |event, elwt| {
        match event {
            Event::DeviceEvent { event: DeviceEvent::MouseMotion { delta }, .. } => { engine.mouse_delta.0 += delta.0 as f32; engine.mouse_delta.1 += delta.1 as f32; }
            Event::WindowEvent { ref event, .. } => match event {
                WindowEvent::CloseRequested => elwt.exit(),
                WindowEvent::KeyboardInput { event, .. } => {
                    if let PhysicalKey::Code(key) = event.physical_key {
                        if event.state == ElementState::Pressed { engine.input.insert(key); }
                        else { engine.input.remove(&key); }
                    }
                }
                WindowEvent::RedrawRequested => { engine.update(); let _ = engine.render(); }
                _ => {}
            }
            Event::AboutToWait => engine.window.request_redraw(),
            _ => {}
        }
    });
}
