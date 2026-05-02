use winit::{
    event::*,
    event_loop::{EventLoop},
    window::WindowBuilder,
    keyboard::{KeyCode, PhysicalKey},
};
use std::collections::HashSet;
use glam::Vec3;

// --- Компоненти нашого ECS ---

#[derive(Debug)]
pub struct Position(pub Vec3);

#[derive(Debug)]
pub struct Velocity(pub Vec3);

#[derive(Debug)]
pub struct Player;

// --- Система введення ---

pub struct InputState {
    keys_pressed: HashSet<KeyCode>,
}

impl InputState {
    pub fn new() -> Self {
        Self {
            keys_pressed: HashSet::new(),
        }
    }

    pub fn update(&mut self, event: &WindowEvent) {
        if let WindowEvent::KeyboardInput { event, .. } = event {
            if let PhysicalKey::Code(key) = event.physical_key {
                match event.state {
                    ElementState::Pressed => { self.keys_pressed.insert(key); }
                    ElementState::Released => { self.keys_pressed.remove(&key); }
                }
            }
        }
    }

    pub fn is_key_pressed(&self, key: KeyCode) -> bool {
        self.keys_pressed.contains(&key)
    }
}

// --- Ядро рушія ---

pub struct Engine {
    pub window: &'static winit::window::Window,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pub size: winit::dpi::PhysicalSize<u32>,
    pub world: hecs::World,
    pub input: InputState,
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

        let adapter = instance.request_adapter(
            &wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::default(),
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
            },
        ).await.unwrap();

        let (device, queue) = adapter.request_device(
            &wgpu::DeviceDescriptor {
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::default(),
                label: None,
            },
            None,
        ).await.unwrap();

        let surface_caps = surface.get_capabilities(&adapter);
        let surface_format = surface_caps.formats.iter()
            .copied()
            .find(|f| f.is_srgb())
            .unwrap_or(surface_caps.formats[0]);

        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            width: size.width,
            height: size.height,
            present_mode: surface_caps.present_modes[0],
            alpha_mode: surface_caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        let mut world = hecs::World::new();
        let input = InputState::new();

        // Створюємо сутність гравця в ECS
        world.spawn((
            Player,
            Position(Vec3::ZERO),
            Velocity(Vec3::ZERO),
        ));

        Self {
            window,
            surface,
            device,
            queue,
            config,
            size,
            world,
            input,
        }
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            self.size = new_size;
            self.config.width = new_size.width;
            self.config.height = new_size.height;
            self.surface.configure(&self.device, &self.config);
        }
    }

    pub fn handle_window_event(&mut self, event: &WindowEvent) -> bool {
        self.input.update(event);
        false
    }

    pub fn update(&mut self) {
        let speed = 0.1;
        let mut move_dir = Vec3::ZERO;

        // Зчитуємо введення
        if self.input.is_key_pressed(KeyCode::KeyW) { move_dir.z -= 1.0; }
        if self.input.is_key_pressed(KeyCode::KeyS) { move_dir.z += 1.0; }
        if self.input.is_key_pressed(KeyCode::KeyA) { move_dir.x -= 1.0; }
        if self.input.is_key_pressed(KeyCode::KeyD) { move_dir.x += 1.0; }

        if move_dir.length() > 0.0 {
            move_dir = move_dir.normalize();
        }

        // Система руху в ECS: оновлюємо позицію всіх сутностей, що мають Velocity та Position
        for (_id, (pos, _player)) in self.world.query_mut::<(&mut Position, &Player)>() {
            pos.0 += move_dir * speed;
            // println!("Позиція гравця: {:?}", pos.0);
        }
    }

    pub fn render(&mut self) -> Result<(), wgpu::SurfaceError> {
        let output = self.surface.get_current_texture()?;
        let view = output.texture.create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Render Encoder"),
        });

        {
            let _render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Render Pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color {
                            r: 0.1,
                            g: 0.2,
                            b: 0.3,
                            a: 1.0,
                        }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
            });
        }

        self.queue.submit(std::iter::once(encoder.finish()));
        output.present();

        Ok(())
    }
}

pub async fn run() {
    env_logger::init();
    let event_loop = EventLoop::new().unwrap();
    let window = WindowBuilder::new()
        .with_title("Unique 3D Engine")
        .build(&event_loop)
        .unwrap();

    let mut engine = Engine::new(window).await;

    let _ = event_loop.run(move |event, elwt| {
        match event {
            Event::WindowEvent {
                ref event,
                window_id,
            } if window_id == engine.window.id() => {
                if !engine.handle_window_event(event) {
                    match event {
                        WindowEvent::CloseRequested
                        | WindowEvent::KeyboardInput {
                            event: KeyEvent {
                                state: ElementState::Pressed,
                                physical_key: PhysicalKey::Code(KeyCode::Escape),
                                ..
                            },
                            ..
                        } => elwt.exit(),
                        WindowEvent::Resized(physical_size) => {
                            engine.resize(*physical_size);
                        }
                        WindowEvent::RedrawRequested => {
                            engine.update();
                            match engine.render() {
                                Ok(_) => {}
                                Err(wgpu::SurfaceError::Lost) => engine.resize(engine.size),
                                Err(wgpu::SurfaceError::OutOfMemory) => elwt.exit(),
                                Err(e) => eprintln!("{:?}", e),
                            }
                        }
                        _ => {}
                    }
                }
            }
            Event::AboutToWait => {
                engine.window.request_redraw();
            }
            _ => {}
        }
    });
}
