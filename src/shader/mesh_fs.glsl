#extension GL_ARB_bindless_texture : require

// ------------------------------------------------------------------
// DEFINES ----------------------------------------------------------
// ------------------------------------------------------------------

#define NUM_INSTANCES 16
#define NUM_SDFS 16
#define INFINITY 100000.0f

// ------------------------------------------------------------------
// OUTPUT VARIABLES  ------------------------------------------------
// ------------------------------------------------------------------

out vec3 FS_OUT_Color;

// ------------------------------------------------------------------
// INPUT VARIABLES  -------------------------------------------------
// ------------------------------------------------------------------

in vec3 FS_IN_WorldPos;
in vec3 FS_IN_Normal;
in vec2 FS_IN_UV;
in vec4 FS_IN_NDCFragPos;

// ------------------------------------------------------------------
// STRUCTURES -------------------------------------------------------
// ------------------------------------------------------------------

struct Instance
{
    mat4  transform;
    vec4  half_extents;
    ivec4 sdf_idx;
};

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

layout(std140, binding = 0) uniform GlobalUniforms
{
    mat4 view_proj;
    vec4 cam_pos;
    int  num_instances;
};

layout(std140, binding = 1) uniform Instances
{
    Instance instances[NUM_INSTANCES];
};

layout(std140, binding = 2) uniform SDFTextures
{
    sampler3D sdf[NUM_SDFS];
};

uniform bool  u_SDFSoftShadows;
uniform float u_SDFTMin;
uniform float u_SDFTMax;
uniform float u_SDFSoftShadowsK;

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

vec3 invert(vec3 p, mat4 t)
{
    return vec3(inverse(t) * vec4(p, 1.0f));
}

// ------------------------------------------------------------------

float evaluate_box_sdf(vec3 p, vec3 b)
{
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// ------------------------------------------------------------------

float evaluate_scene_sdf(vec3 p, out vec3 sample_pos, out Instance instance)
{
    float dist_to_box = INFINITY;

    for (int i = 0; i < num_instances; i++)
    {
        sample_pos = invert(p, instances[i].transform);
        float t    = evaluate_box_sdf(sample_pos, instances[i].half_extents.xyz);

        if (t < dist_to_box)
        {
            dist_to_box = t;
            instance    = instances[i];
        }
    }

    return dist_to_box;
}

// ------------------------------------------------------------------

float evaluate_mesh_sdf(vec3 p, in Instance instance)
{
    vec3 uvw = (p / instance.half_extents.xyz) * 0.5f + vec3(0.5f);
    return textureLod(sdf[instance.sdf_idx.x], uvw, 0.0f).r;
}

// ------------------------------------------------------------------

float shadow_ray_march(vec3 ro, vec3 rd, float k)
{
    float res = 1.0;

    for (float t = u_SDFTMin; t < u_SDFTMax;)
    {
        vec3     sample_pos;
        Instance instance;

        float dist_to_box  = evaluate_scene_sdf(ro + rd * t, sample_pos, instance);
        float dist_to_mesh = evaluate_mesh_sdf(sample_pos, instance);

        float h = max(dist_to_box, 0.0f) + dist_to_mesh;

        if (h < 0.001f)
            return 0.0f;

        if (u_SDFSoftShadows)
            res = min(res, k * h / t);

        t += h;
    }

    return res;
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    vec3 albedo = vec3(0.5f);
    vec3 L      = normalize(-vec3(1.0f, -1.0f, 0.0f));
    vec3 N      = normalize(FS_IN_Normal);

    float shadow = shadow_ray_march(FS_IN_WorldPos, L, u_SDFSoftShadowsK);

    FS_OUT_Color = albedo * clamp(dot(N, L), 0.0, 1.0) * shadow + albedo * 0.1f;
}

// ------------------------------------------------------------------
