struct CameraUniform {
    view_proj: mat4x4<f32>,
    view_pos: vec3<f32>,
    _padding: f32,
    time: f32,
};
@group(0) @binding(0)
var<uniform> camera: CameraUniform;

struct ModelUniform {
    model_matrix: mat4x4<f32>,
};
@group(1) @binding(0)
var<uniform> model: ModelUniform;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) normal: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) world_pos: vec3<f32>,
};

@vertex
fn vs_main(model_in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    var pos = model_in.position;
    var normal = model_in.normal;

    // М'ЯКІ ХВИЛІ
    if (pos.z < -2.0) {
        let wave = sin(pos.x * 0.12 + camera.time * 1.5) * 0.4 + cos(pos.y * 0.08 + camera.time * 1.2) * 0.3;
        pos.z += wave;
        normal = normalize(vec3<f32>(-0.05 * cos(pos.x * 0.1), -0.05 * sin(pos.y * 0.1), 1.0));
    }

    let world_pos = model.model_matrix * vec4<f32>(pos, 1.0);
    out.clip_position = camera.view_proj * world_pos;
    out.color = model_in.color;
    out.normal = (model.model_matrix * vec4<f32>(normal, 0.0)).xyz;
    out.world_pos = world_pos.xyz;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let light_dir = normalize(vec3<f32>(0.3, 0.5, 0.8));
    let view_dir = normalize(camera.view_pos - in.world_pos);
    let half_dir = normalize(light_dir + view_dir);
    let normal = normalize(in.normal);
    
    // ОСВІТЛЕННЯ БЕЗ ШУМУ
    let diffuse = max(dot(normal, light_dir), 0.0);
    
    // Бліки
    let is_water = step(in.world_pos.z, -1.0);
    let spec_pow = mix(32.0, 128.0, is_water);
    let spec_int = mix(0.2, 0.6, is_water);
    let spec = pow(max(dot(normal, half_dir), 0.0), spec_pow) * spec_int;
    
    let ambient = mix(0.3, 0.5, clamp(in.world_pos.z * 0.02, 0.0, 1.0));
    let light_factor = diffuse + ambient;
    
    var final_color = in.color * light_factor + vec3<f32>(1.0, 1.0, 0.9) * spec;
    
    // КРИШТАЛЕВО ЧИСТИЙ ТУМАН
    let dist = distance(camera.view_pos, in.world_pos);
    let fog_factor = clamp((dist - 100.0) / 700.0, 0.0, 1.0);
    
    // Градієнт неба
    let sky_top = vec3<f32>(0.2, 0.5, 0.9);
    let sky_horizon = vec3<f32>(0.6, 0.8, 1.0);
    let sky_final = mix(sky_horizon, sky_top, clamp(view_dir.z * 2.0, 0.0, 1.0));
    
    final_color = mix(final_color, sky_final, fog_factor);
    
    return vec4<f32>(final_color, 1.0);
}
