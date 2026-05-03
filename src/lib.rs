use winit::{
    event::*,
    event_loop::{EventLoop},
    window::WindowBuilder,
    keyboard::{KeyCode, PhysicalKey},
};
use std::collections::HashSet;
use glam::{Vec3, Mat4};
use wgpu::util::DeviceExt;
use noise::{NoiseFn, Perlin};
use instant::Instant;

// --- Типи ---

#[repr(C)]
#[derive(Copy, Clone, Debug, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Vertex { 
    position: [f32; 3], 
    color: [f32; 3],
    normal: [f32; 3],
}

impl Vertex {
    fn desc() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: 36,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute { offset: 0, shader_location: 0, format: wgpu::VertexFormat::Float32x3 },
                wgpu::VertexAttribute { offset: 12, shader_location: 1, format: wgpu::VertexFormat::Float32x3 },
                wgpu::VertexAttribute { offset: 24, shader_location: 2, format: wgpu::VertexFormat::Float32x3 },
            ],
        }
    }
}

pub struct Camera {
    pub eye: Vec3, pub target: Vec3, pub up: Vec3,
    pub aspect: f32, pub fovy: f32, pub yaw: f32, pub pitch: f32,
}

pub struct Engine {
    pub window: &'static winit::window::Window,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    render_pipeline: wgpu::RenderPipeline,
    depth_view: wgpu::TextureView,
    msaa_view: wgpu::TextureView,
    
    cube_vb: wgpu::Buffer, num_cube_verts: u32,
    terrain_vb: wgpu::Buffer, terrain_ib: wgpu::Buffer, num_terrain_idx: u32,
    water_vb: wgpu::Buffer,

    pub camera: Camera,
    camera_buf: wgpu::Buffer, camera_bg: wgpu::BindGroup,
    model_buf: wgpu::Buffer, model_bg: wgpu::BindGroup,

    pub input: HashSet<KeyCode>,
    pub mouse_delta: (f32, f32),
    pub player_pos: Vec3,
    pub player_vel: Vec3,
    pub perlin: Perlin,
    start_time: Instant,
    last_frame: Instant,
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

        let msaa_tex = device.create_texture(&wgpu::TextureDescriptor {
            label: None, size: wgpu::Extent3d { width: config.width, height: config.height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 4, dimension: wgpu::TextureDimension::D2, format: config.format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
        });
        let msaa_view = msaa_tex.create_view(&wgpu::TextureViewDescriptor::default());

        let depth_tex = device.create_texture(&wgpu::TextureDescriptor {
            label: None, size: wgpu::Extent3d { width: config.width, height: config.height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 4, dimension: wgpu::TextureDimension::D2, format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
        });
        let depth_view = depth_tex.create_view(&wgpu::TextureViewDescriptor::default());

        let camera_buf = device.create_buffer(&wgpu::BufferDescriptor { label: None, size: 96, usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST, mapped_at_creation: false });
        let camera_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor { entries: &[wgpu::BindGroupLayoutEntry { binding: 0, visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }], label: None });
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
            depth_stencil: Some(wgpu::DepthStencilState { format: wgpu::TextureFormat::Depth32Float, depth_write_enabled: true, depth_compare: wgpu::CompareFunction::Less, stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default() }),
            multisample: wgpu::MultisampleState { count: 4, mask: !0, alpha_to_coverage_enabled: false },
            multiview: None,
        });

        // Куб гравця
        let mut cube_v = Vec::new();
        let faces = [
            ([0,0,1], [[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]]), ([0,0,-1], [[-1,1,-1],[1,1,-1],[1,-1,-1],[-1,-1,-1]]),
            ([0,1,0], [[-1,1,1],[1,1,1],[1,1,-1],[-1,1,-1]]), ([0,-1,0], [[-1,-1,-1],[1,-1,-1],[1,-1,1],[-1,-1,1]]),
            ([1,0,0], [[1,-1,1],[1,-1,-1],[1,1,-1],[1,1,1]]), ([-1,0,0], [[-1,-1,-1],[-1,-1,1],[-1,1,1],[-1,1,-1]]),
        ];
        for (n, quad) in faces {
            let color = [0.8, 0.2, 0.1];
            let n_f = [n[0] as f32, n[1] as f32, n[2] as f32];
            let v0 = Vertex { position: [quad[0][0] as f32 * 0.5, quad[0][1] as f32 * 0.5, quad[0][2] as f32 * 0.5], color, normal: n_f };
            let v1 = Vertex { position: [quad[1][0] as f32 * 0.5, quad[1][1] as f32 * 0.5, quad[1][2] as f32 * 0.5], color, normal: n_f };
            let v2 = Vertex { position: [quad[2][0] as f32 * 0.5, quad[2][1] as f32 * 0.5, quad[2][2] as f32 * 0.5], color, normal: n_f };
            let v3 = Vertex { position: [quad[3][0] as f32 * 0.5, quad[3][1] as f32 * 0.5, quad[3][2] as f32 * 0.5], color, normal: n_f };
            cube_v.extend_from_slice(&[v0, v1, v2, v0, v2, v3]);
        }
        let cube_vb = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&cube_v), usage: wgpu::BufferUsages::VERTEX });

        // Ландшафт
        let mut terrain_verts = Vec::new(); let mut terrain_idx = Vec::new();
        let t_size = 600.0; let t_res = 120;
        let perlin = Perlin::new(1);
        for y in 0..t_res {
            for x in 0..t_res {
                let fx = (x as f32 / (t_res-1) as f32) * t_size - t_size / 2.0;
                let fy = (y as f32 / (t_res-1) as f32) * t_size - t_size / 2.0;
                let h = perlin.get([fx as f64 * 0.01, fy as f64 * 0.01]) as f32 * 25.0;
                let h_x = perlin.get([(fx+0.2) as f64 * 0.01, fy as f64 * 0.01]) as f32 * 25.0;
                let h_y = perlin.get([fx as f64 * 0.01, (fy+0.2) as f64 * 0.01]) as f32 * 25.0;
                let normal = Vec3::new(h - h_x, h - h_y, 0.2).normalize();
                let color = if h > 15.0 { [0.8, 0.8, 0.8] } else if h < 0.2 { [0.7, 0.6, 0.4] } else { [0.2, 0.5, 0.2] };
                terrain_verts.push(Vertex { position: [fx, fy, h], color, normal: normal.into() });
            }
        }
        for y in 0..t_res - 1 {
            for x in 0..t_res - 1 {
                let i = (y * t_res + x) as u16;
                terrain_idx.extend_from_slice(&[i, i + t_res as u16, i + 1, i + 1, i + t_res as u16, i + t_res as u16 + 1]);
            }
        }
        let terrain_vb = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&terrain_verts), usage: wgpu::BufferUsages::VERTEX });
        let terrain_ib = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(&terrain_idx), usage: wgpu::BufferUsages::INDEX });

        // ВОДА: ТЕПЕР СУЦІЛЬНА ПЛОЩИНА (БЕЗ ДІРОК)
        let w_size = 1500.0;
        let water_v = &[
            Vertex { position: [-w_size, -w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
            Vertex { position: [ w_size, -w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
            Vertex { position: [ w_size,  w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
            Vertex { position: [-w_size, -w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
            Vertex { position: [ w_size,  w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
            Vertex { position: [-w_size,  w_size, -2.5], color: [0.1, 0.2, 0.5], normal: [0.0,0.0,1.0] },
        ];
        let water_vb = device.create_buffer_init(&wgpu::util::BufferInitDescriptor { label: None, contents: bytemuck::cast_slice(water_v), usage: wgpu::BufferUsages::VERTEX });

        let aspect = config.width as f32 / config.height as f32;
        Self {
            window, surface, device, queue, config, render_pipeline, depth_view, msaa_view,
            cube_vb, num_cube_verts: cube_v.len() as u32,
            terrain_vb, terrain_ib, num_terrain_idx: terrain_idx.len() as u32,
            water_vb,
            camera: Camera { eye: Vec3::ZERO, target: Vec3::ZERO, up: Vec3::Z, aspect, fovy: 45.0, yaw: 90.0, pitch: 30.0 },
            camera_buf, camera_bg, model_buf, model_bg,
            input: HashSet::new(), mouse_delta: (0.0, 0.0),
            player_pos: Vec3::new(0.0, 0.0, 50.0), player_vel: Vec3::ZERO,
            perlin, start_time: Instant::now(), last_frame: Instant::now(),
        }
    }

    pub fn update(&mut self) {
        let dt = self.last_frame.elapsed().as_secs_f32().min(0.033);
        let time = self.start_time.elapsed().as_secs_f32();
        self.last_frame = Instant::now();

        self.camera.yaw += self.mouse_delta.0 * 0.12;
        self.camera.pitch = (self.camera.pitch - self.mouse_delta.1 * 0.12).clamp(5.0, 85.0);
        self.mouse_delta = (0.0, 0.0);

        self.player_vel.z += -40.0 * dt;
        let mut move_dir = Vec3::ZERO;
        let y_rad = self.camera.yaw.to_radians();
        let fwd = Vec3::new(y_rad.cos(), y_rad.sin(), 0.0).normalize();
        let rgt = Vec3::new(y_rad.sin(), -y_rad.cos(), 0.0).normalize();
        if self.input.contains(&KeyCode::KeyW) { move_dir += fwd; }
        if self.input.contains(&KeyCode::KeyS) { move_dir -= fwd; }
        if self.input.contains(&KeyCode::KeyA) { move_dir += rgt; }
        if self.input.contains(&KeyCode::KeyD) { move_dir -= rgt; }
        if move_dir.length() > 0.0 {
            let v = move_dir.normalize() * 25.0;
            self.player_vel.x = v.x; self.player_vel.y = v.y;
        } else {
            self.player_vel.x *= 0.8; self.player_vel.y *= 0.8;
        }

        self.player_pos += self.player_vel * dt;
        let gh = self.perlin.get([self.player_pos.x as f64 * 0.01, self.player_pos.y as f64 * 0.01]) as f32 * 25.0;
        if self.player_pos.z < gh + 1.0 {
            self.player_pos.z = gh + 1.0;
            if self.player_vel.z < 0.0 { self.player_vel.z = 0.0; }
        }
        if self.input.contains(&KeyCode::Space) && (self.player_pos.z - gh - 1.0).abs() < 0.2 {
            self.player_vel.z = 18.0;
        }

        let p_rad = self.camera.pitch.to_radians();
        let y_rad = self.camera.yaw.to_radians();
        let dist = 35.0;
        self.camera.eye = Vec3::new(self.player_pos.x - dist * p_rad.cos() * y_rad.cos(), self.player_pos.y - dist * p_rad.cos() * y_rad.sin(), self.player_pos.z + dist * p_rad.sin());
        let vm = Mat4::perspective_rh(self.camera.fovy.to_radians(), self.camera.aspect, 0.1, 5000.0) * Mat4::look_at_rh(self.camera.eye, self.player_pos, self.camera.up);
        
        let mut cam_data = [0.0f32; 24];
        cam_data[0..16].copy_from_slice(&vm.to_cols_array());
        cam_data[16..19].copy_from_slice(&self.camera.eye.to_array());
        cam_data[20] = time;
        self.queue.write_buffer(&self.camera_buf, 0, bytemuck::cast_slice(&cam_data));
    }

    pub fn render(&mut self) -> Result<(), wgpu::SurfaceError> {
        let out = self.surface.get_current_texture()?;
        let view = out.texture.create_view(&wgpu::TextureViewDescriptor::default());
        let mut enc = self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
        {
            let mut rp = enc.begin_render_pass(&wgpu::RenderPassDescriptor {
                color_attachments: &[Some(wgpu::RenderPassColorAttachment { 
                    view: &self.msaa_view, resolve_target: Some(&view), 
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.5, g: 0.7, b: 0.9, a: 1.0 }), store: wgpu::StoreOp::Store } 
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment { view: &self.depth_view, depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store }), stencil_ops: None }),
                ..Default::default()
            });
            rp.set_pipeline(&self.render_pipeline);
            rp.set_bind_group(0, &self.camera_bg, &[]);
            self.queue.write_buffer(&self.model_buf, 0, bytemuck::cast_slice(&[Mat4::IDENTITY.to_cols_array_2d()]));
            rp.set_bind_group(1, &self.model_bg, &[]);
            rp.set_vertex_buffer(0, self.water_vb.slice(..));
            rp.draw(0..6, 0..1); // ТЕПЕР ЦЕ СУЦІЛЬНА ПЛОЩИНА
            rp.set_vertex_buffer(0, self.terrain_vb.slice(..));
            rp.set_index_buffer(self.terrain_ib.slice(..), wgpu::IndexFormat::Uint16);
            rp.draw_indexed(0..self.num_terrain_idx, 0, 0..1);
            let m = Mat4::from_translation(self.player_pos);
            self.queue.write_buffer(&self.model_buf, 0, bytemuck::cast_slice(&[m.to_cols_array_2d()]));
            rp.set_bind_group(1, &self.model_bg, &[]);
            rp.set_vertex_buffer(0, self.cube_vb.slice(..));
            rp.draw(0..self.num_cube_verts, 0..1);
        }
        self.queue.submit(std::iter::once(enc.finish()));
        out.present(); Ok(())
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
