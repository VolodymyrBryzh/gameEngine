use raylib::prelude::*;
use noise::{NoiseFn, Perlin};

pub fn run() {
    // 1. Ініціалізація
    let (mut rl, thread) = raylib::init()
        .size(1280, 720)
        .title("Island Survival - Final Attempt")
        .msaa_4x()
        .build();

    rl.set_target_fps(60);
    rl.disable_cursor();

    // 2. Камера (Перша особа)
    let mut camera = Camera3D::custom(
        Vector3::new(0.0, 50.0, 50.0), // Позиція
        Vector3::new(0.0, 0.0, 0.0),   // Куди дивимось
        Vector3::new(0.0, 1.0, 0.0),   // Вгору
        45.0,
        CameraProjection::CAMERA_PERSPECTIVE,
    );

    // 3. Генерація ландшафту
    let perlin = Perlin::new(1);
    let mut player_pos = Vector3::new(0.0, 30.0, 0.0);
    let mut player_vel_y = 0.0f32;

    // Створюємо меш острова для "WOW" ефекту
    let terrain_size = 100;
    let terrain_scale = 4.0;
    
    // Основний цикл
    while !rl.window_should_close() {
        let dt = rl.get_frame_time();

        // --- ЛОГІКА ТА КЕРУВАННЯ ---
        rl.update_camera(&mut camera, CameraMode::CAMERA_FIRST_PERSON);
        player_pos = camera.position; // Камера і є наш гравець

        // Проста фізика (Зіткнення з землею)
        let ground_h = perlin.get([
            player_pos.x as f64 * 0.02, 
            player_pos.z as f64 * 0.02
        ]) as f32 * 15.0;

        // Гравітація та стрибок
        if player_pos.y > ground_h + 2.0 {
            player_vel_y -= 30.0 * dt;
        } else {
            player_pos.y = ground_h + 2.0;
            player_vel_y = 0.0;
            if rl.is_key_pressed(KeyboardKey::KEY_SPACE) {
                player_vel_y = 12.0;
            }
        }
        
        camera.position.y += player_vel_y * dt;
        // Корекція цілі камери, щоб вона не "плавала"
        let forward = camera.target - camera.position;
        camera.target = camera.position + forward;

        // --- МАЛЮВАННЯ ---
        let mut d = rl.begin_drawing(&thread);
        d.clear_background(Color::SKYBLUE);

        {
            let mut d3 = d.begin_mode3D(&camera);

            // 1. Малюємо океан
            d3.draw_plane(Vector3::new(0.0, 0.0, 0.0), Vector2::new(1000.0, 1000.0), Color::new(0, 105, 148, 255));

            // 2. Малюємо острів (сітка кубів або меш)
            for x in -25..25 {
                for z in -25..25 {
                    let fx = x as f32 * terrain_scale;
                    let fz = z as f32 * terrain_scale;
                    let h = perlin.get([fx as f64 * 0.02, fz as f64 * 0.02]) as f32 * 15.0;
                    
                    let color = if h > 10.0 { Color::GRAY } 
                               else if h < 1.0 { Color::BEIGE } 
                               else { Color::DARKGREEN };
                               
                    // Малюємо блоки ландшафту
                    if h > 0.0 {
                        d3.draw_cube(
                            Vector3::new(fx, h / 2.0, fz),
                            terrain_scale, h, terrain_scale,
                            color
                        );
                    }
                }
            }

            // 3. Малюємо сонце
            d3.draw_sphere(Vector3::new(100.0, 200.0, 100.0), 10.0, Color::YELLOW);
        }

        d.draw_fps(10, 10);
        d.draw_text("WASD to Move | SPACE to Jump", 10, 40, 20, Color::WHITE);
        d.draw_text(&format!("Z Position: {:.2}", player_pos.y), 10, 70, 20, Color::YELLOW);
    }
}
