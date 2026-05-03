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
    
    // М'ЯКЕ ПЛИВУЧЕ ПЕРЕСУВАННЯ ВОДИ (БЕЗ ДІРОК)
    if (pos.z < -2.0) {
        pos.z += sin(camera.time * 1.2) * 0.3;
    }

    let world_pos = model.model_matrix * vec4<f32>(pos, 1.0);
    out.clip_position = camera.view_proj * world_pos;
    out.color = model_in.color;
    out.normal = (model.model_matrix * vec4<f32>(model_in.normal, 0.0)).xyz;
    out.world_pos = world_pos.xyz;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let light_dir = normalize(vec3<f32>(0.5, 0.5, 1.0));
    let view_dir = normalize(camera.view_pos - in.world_pos);
    let half_dir = normalize(light_dir + view_dir);
    let normal = normalize(in.normal);
    
    let diffuse = max(dot(normal, light_dir), 0.0);
    
    // Плавні бліки
    let is_water = step(in.world_pos.z, -1.0);
    let spec = pow(max(dot(normal, half_dir), 0.0), 64.0) * mix(0.1, 0.5, is_water);
    
    let ambient = 0.4;
    let final_color = in.color * (diffuse + ambient) + vec3<f32>(1.0, 1.0, 0.9) * spec;
    
    // ТУМАН, ЩО ЗЛИВАЄТЬСЯ З НЕБОМ
    let dist = distance(camera.view_pos, in.world_pos);
    let fog_factor = clamp((dist - 150.0) / 800.0, 0.0, 1.0);
    
    let sky_color = vec3<f32>(0.5, 0.7, 0.9);
    return vec4<f32>(mix(final_color, sky_color, fog_factor), 1.0);
}
