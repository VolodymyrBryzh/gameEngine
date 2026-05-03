// Shader з підтримкою світла

struct CameraUniform {
    view_proj: mat4x4<f32>,
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
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) world_position: vec3<f32>,
};

@vertex
fn vs_main(
    vertex: VertexInput,
) -> VertexOutput {
    var out: VertexOutput;
    out.color = vertex.color;
    let world_pos = model.model_matrix * vec4<f32>(vertex.position, 1.0);
    out.world_position = world_pos.xyz;
    out.clip_position = camera.view_proj * world_pos;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Просте спрямоване світло (як Сонце)
    let light_dir = normalize(vec3<f32>(0.5, 1.0, 0.5));
    
    // Оскільки у нас поки немає чесних нормалей мешу, 
    // ми використаємо невеликий хак для об'єму або просто зафарбуємо.
    // Але для землі ми розрахуємо нормаль у наступному кроці.
    
    let ambient = 0.3;
    let diffuse = max(dot(vec3<f32>(0.0, 1.0, 0.0), light_dir), 0.0);
    
    let color = in.color * (ambient + diffuse);
    return vec4<f32>(color, 1.0);
}
