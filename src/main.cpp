#define _USE_MATH_DEFINES
#include <ogl.h>
#include <application.h>
#include <mesh.h>
#include <camera.h>
#include <material.h>
#include <memory>
#include <iostream>
#include <stack>
#include <random>
#include <chrono>
#include <random>
#include <fstream>

#define CAMERA_FAR_PLANE 1000.0f
#define NUM_INSTANCES 16
#define NUM_SDFS 16

struct GlobalUniforms
{
    DW_ALIGNED(16)
    glm::mat4 view_proj;
    DW_ALIGNED(16)
    glm::vec4 cam_pos;
    DW_ALIGNED(16)
    glm::vec4 light_direction;
    DW_ALIGNED(16)
    int32_t num_instances;
};

struct InstanceUniforms
{
    DW_ALIGNED(16)
    glm::mat4 transform;
    DW_ALIGNED(16)
    glm::vec4 half_extents;
    DW_ALIGNED(16)
    glm::ivec4 sdf_idx;
};

struct Instance
{
    // Mesh
    dw::Mesh::Ptr mesh;

    // SDF
    dw::gl::Texture3D::Ptr sdf;
    glm::ivec3             volume_size;
    glm::vec3              grid_origin;
    float                  grid_step_size;
    glm::vec3              min_extents;
    glm::vec3              max_extents;

    // Transform
    glm::vec3 position  = glm::vec3(0.0f);
    glm::mat4 transform = glm::mat4(1.0f);
};

class SDFShadows : public dw::Application
{
protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool init(int argc, const char* argv[]) override
    {
        // Create GPU resources.
        if (!create_shaders())
            return false;

        if (!create_uniform_buffer())
            return false;

        // Load scene.
        if (!load_scene())
            return false;

        update_textures();

        // Create camera.
        create_camera();

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update(double delta) override
    {
        if (m_debug_gui)
            debug_gui();

        // Update camera.
        update_camera();

        update_uniforms();

        render_scene();

        m_debug_draw.set_depth_test(true);

        if (m_draw_bounding_boxes)
        {
            for (const auto& instance : m_instances)
                m_debug_draw.obb(instance.min_extents, instance.max_extents, instance.transform, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        m_debug_draw.render(nullptr, m_width, m_height, m_main_camera->m_view_projection, m_main_camera->m_position);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void debug_gui()
    {
        ImGui::Checkbox("Draw Bounding Boxes", &m_draw_bounding_boxes);
        ImGui::Checkbox("Soft Shadows", &m_soft_shadows);
        ImGui::InputFloat("T-Min", &m_t_min);
        ImGui::InputFloat("T-Max", &m_t_max);
        ImGui::SliderFloat("Soft Shadows K", &m_soft_shadows_k, 1.0f, 16.0f);
        ImGui::SliderFloat("Light Pitch", &m_light_pitch, -1.0f, 1.0f);

        ImGui::Separator();

        for (int i = 0; i < m_instances.size(); i++)
        {
            auto& instance = m_instances[i];

            ImGui::PushID(i);
            ImGui::Text("Mesh %i", i);
            ImGui::InputFloat3("Position", &instance.position.x);
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void window_resized(int width, int height) override
    {
        // Override window resized method to update camera projection.
        m_main_camera->update_projection(60.0f, 1.0f, CAMERA_FAR_PLANE, float(m_width) / float(m_height));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_pressed(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W)
            m_heading_speed = m_camera_speed;
        else if (code == GLFW_KEY_S)
            m_heading_speed = -m_camera_speed;

        // Handle sideways movement.
        if (code == GLFW_KEY_A)
            m_sideways_speed = -m_camera_speed;
        else if (code == GLFW_KEY_D)
            m_sideways_speed = m_camera_speed;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = true;

        if (code == GLFW_KEY_G)
            m_debug_gui = !m_debug_gui;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_released(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W || code == GLFW_KEY_S)
            m_heading_speed = 0.0f;

        // Handle sideways movement.
        if (code == GLFW_KEY_A || code == GLFW_KEY_D)
            m_sideways_speed = 0.0f;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_pressed(int code) override
    {
        // Enable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_released(int code) override
    {
        // Disable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    dw::AppSettings intial_app_settings() override
    {
        dw::AppSettings settings;

        settings.maximized             = false;
        settings.major_ver             = 4;
        settings.width                 = 1920;
        settings.height                = 1080;
        settings.title                 = "SDF Shadows (c) 2021 Dihara Wijetunga";
        settings.enable_debug_callback = false;

        return settings;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_shaders()
    {
        // Create general shaders
        m_mesh_vs     = dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/mesh_vs.glsl");
        m_mesh_fs     = dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/mesh_fs.glsl");
        m_bake_sdf_cs = dw::gl::Shader::create_from_file(GL_COMPUTE_SHADER, "shader/bake_sdf_cs.glsl");

        if (!m_mesh_vs || !m_mesh_fs || !m_bake_sdf_cs)
        {
            DW_LOG_FATAL("Failed to create Shaders");
            return false;
        }

        // Create general shader program
        m_mesh_program = dw::gl::Program::create({ m_mesh_vs, m_mesh_fs });

        if (!m_mesh_program)
        {
            DW_LOG_FATAL("Failed to create Shader Program");
            return false;
        }

        // Create general shader program
        m_bake_sdf_program = dw::gl::Program::create({ m_bake_sdf_cs });

        if (!m_bake_sdf_program)
        {
            DW_LOG_FATAL("Failed to create Shader Program");
            return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_uniform_buffer()
    {
        // Create uniform buffer for global data
        m_global_ubo   = dw::gl::Buffer::create(GL_UNIFORM_BUFFER, GL_MAP_WRITE_BIT, sizeof(GlobalUniforms));
        m_instance_ubo = dw::gl::Buffer::create(GL_UNIFORM_BUFFER, GL_MAP_WRITE_BIT, sizeof(InstanceUniforms) * NUM_INSTANCES);
        m_sdf_ubo      = dw::gl::Buffer::create(GL_UNIFORM_BUFFER, GL_MAP_WRITE_BIT, sizeof(uint64_t) * NUM_SDFS * 2);

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void bake_sdf(Instance& instance, float grid_step_size, int padding)
    {
        glm::vec3  min_extents = instance.mesh->min_extents() - (glm::vec3(grid_step_size) * float(padding));
        glm::vec3  max_extents = instance.mesh->max_extents() + (glm::vec3(grid_step_size) * float(padding));
        glm::vec3  grid_origin = min_extents + glm::vec3(grid_step_size / 2.0f);
        glm::vec3  box_size    = max_extents - min_extents;
        glm::ivec3 volume_size = glm::ivec3(glm::ceil(box_size / glm::vec3(grid_step_size)));

        instance.volume_size    = volume_size;
        instance.grid_origin    = grid_origin;
        instance.grid_step_size = grid_step_size;
        instance.min_extents    = min_extents;
        instance.max_extents    = max_extents;

        instance.sdf = dw::gl::Texture3D::create(volume_size.x, volume_size.y, volume_size.z, 1, GL_R32F, GL_RED, GL_FLOAT);
        instance.sdf->set_min_filter(GL_LINEAR);
        instance.sdf->set_mag_filter(GL_LINEAR);
        instance.sdf->set_wrapping(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

        m_bake_sdf_program->use();

        m_bake_sdf_program->set_uniform("u_GridStepSize", glm::vec3(grid_step_size));
        m_bake_sdf_program->set_uniform("u_GridOrigin", grid_origin);
        m_bake_sdf_program->set_uniform("u_NumTriangles", static_cast<uint32_t>(instance.mesh->indices().size() / 3));
        m_bake_sdf_program->set_uniform("u_VolumeSize", volume_size);

        instance.sdf->bind_image(0, 0, 0, GL_READ_WRITE, instance.sdf->internal_format());

        instance.mesh->vertex_buffer()->bind_base(GL_SHADER_STORAGE_BUFFER, 0);
        instance.mesh->index_buffer()->bind_base(GL_SHADER_STORAGE_BUFFER, 1);

        const uint32_t NUM_THREADS_X = 8;
        const uint32_t NUM_THREADS_Y = 8;
        const uint32_t NUM_THREADS_Z = 1;

        uint32_t size_x = static_cast<uint32_t>(ceil(float(volume_size.x) / float(NUM_THREADS_X)));
        uint32_t size_y = static_cast<uint32_t>(ceil(float(volume_size.y) / float(NUM_THREADS_Y)));
        uint32_t size_z = static_cast<uint32_t>(ceil(float(volume_size.z) / float(NUM_THREADS_Z)));

        glDispatchCompute(size_x, size_y, size_z);

        glFinish();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool load_mesh(const std::string& name)
    {
        Instance instance;

        instance.mesh = dw::Mesh::load("mesh/" + name + ".obj");

        if (!instance.mesh)
        {
            DW_LOG_FATAL("Failed to load mesh: " + name);
            return false;
        }

        bake_sdf(instance, 0.025f, 4);

        m_instances.push_back(instance);

        InstanceUniforms uniform;

        uniform.transform    = instance.transform;
        uniform.half_extents = glm::vec4((instance.max_extents - instance.min_extents) / 2.0f, 0.0f);
        uniform.sdf_idx      = glm::ivec4(m_texture_uniforms.size(), 0, 0, 0);

        m_instance_uniforms.push_back(uniform);
        m_texture_uniforms.push_back(instance.sdf->make_texture_handle_resident());

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool load_scene()
    {
        std::string meshes[] = {
            "bunny"
        };

        for (auto mesh : meshes)
        {
            if (!load_mesh(mesh))
            {
                DW_LOG_FATAL("Failed to create mesh instance: " + mesh);
                return false;
            }
        }

        m_ground = dw::Mesh::load("mesh/ground.obj");

        if (!m_ground)
        {
            DW_LOG_FATAL("Failed to load mesh: plane");
            return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_camera()
    {
        m_main_camera = std::make_unique<dw::Camera>(60.0f, 1.0f, CAMERA_FAR_PLANE, float(m_width) / float(m_height), glm::vec3(50.0f, 20.0f, 0.0f), glm::vec3(-1.0f, 0.0, 0.0f));
        m_main_camera->set_rotatation_delta(glm::vec3(0.0f, -90.0f, 0.0f));
        m_main_camera->update();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_mesh(dw::Mesh::Ptr mesh, glm::mat4 model, glm::vec3 color)
    {
        m_mesh_program->set_uniform("u_Model", model);

        // Bind vertex array.
        mesh->mesh_vertex_array()->bind();

        const auto& submeshes = mesh->sub_meshes();

        for (uint32_t i = 0; i < submeshes.size(); i++)
        {
            const dw::SubMesh& submesh = submeshes[i];

            // Issue draw call.
            glDrawElementsBaseVertex(GL_TRIANGLES, submesh.index_count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned int) * submesh.base_index), submesh.base_vertex);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_scene()
    {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_width, m_height);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Bind shader program.
        m_mesh_program->use();

        // Bind SDF texture
        m_mesh_program->set_uniform("u_SDFSoftShadows", m_soft_shadows);
        m_mesh_program->set_uniform("u_SDFTMin", m_t_min);
        m_mesh_program->set_uniform("u_SDFTMax", m_t_max);
        m_mesh_program->set_uniform("u_SDFSoftShadowsK", m_soft_shadows_k);

        // Bind uniform buffers.
        m_global_ubo->bind_base(0);
        m_instance_ubo->bind_base(1);
        m_sdf_ubo->bind_base(2);

        // Draw scene.
        render_mesh(m_ground, glm::mat4(1.0f), glm::vec3(0.5f));

        for (const auto& instance : m_instances)
            render_mesh(instance.mesh, instance.transform, glm::vec3(0.5f));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_uniforms()
    {
        // Global
        {
            void* ptr = m_global_ubo->map(GL_WRITE_ONLY);
            memcpy(ptr, &m_global_uniforms, sizeof(GlobalUniforms));
            m_global_ubo->unmap();
        }

        // Instance
        {
            void* ptr = m_instance_ubo->map(GL_WRITE_ONLY);
            memcpy(ptr, m_instance_uniforms.data(), sizeof(InstanceUniforms) * m_instances.size());
            m_instance_ubo->unmap();
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_textures()
    {
        uint64_t* ptr = (uint64_t*)m_sdf_ubo->map(GL_WRITE_ONLY);

        for (int i = 0; i < m_texture_uniforms.size(); i++)
        {
            memcpy(ptr, &m_texture_uniforms[i], sizeof(uint64_t));
            ptr += 2;
        }

        m_sdf_ubo->unmap();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_transforms(dw::Camera* camera)
    {
        // Update camera matrices.
        m_global_uniforms.view_proj       = camera->m_projection * camera->m_view;
        m_global_uniforms.cam_pos         = glm::vec4(camera->m_position, 0.0f);
        m_global_uniforms.light_direction = glm::vec4(glm::normalize(glm::vec3(0.0f, m_light_pitch, -1.0f)), 0.0f);
        m_global_uniforms.num_instances   = m_instances.size();

        for (int i = 0; i < m_instances.size(); i++)
        {
            auto& instance                   = m_instances[i];
            instance.transform               = glm::translate(glm::mat4(1.0f), instance.position);
            m_instance_uniforms[i].transform = instance.transform;
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_camera()
    {
        dw::Camera* current = m_main_camera.get();

        float forward_delta = m_heading_speed * m_delta;
        float right_delta   = m_sideways_speed * m_delta;

        current->set_translation_delta(current->m_forward, forward_delta);
        current->set_translation_delta(current->m_right, right_delta);

        m_camera_x = m_mouse_delta_x * m_camera_sensitivity;
        m_camera_y = m_mouse_delta_y * m_camera_sensitivity;

        if (m_mouse_look)
        {
            // Activate Mouse Look
            current->set_rotatation_delta(glm::vec3((float)(m_camera_y),
                                                    (float)(m_camera_x),
                                                    (float)(0.0f)));
        }
        else
        {
            current->set_rotatation_delta(glm::vec3((float)(0),
                                                    (float)(0),
                                                    (float)(0)));
        }

        current->update();
        update_transforms(current);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // General GPU resources.
    dw::gl::Shader::Ptr  m_mesh_fs;
    dw::gl::Shader::Ptr  m_mesh_vs;
    dw::gl::Shader::Ptr  m_bake_sdf_cs;
    dw::gl::Program::Ptr m_mesh_program;
    dw::gl::Program::Ptr m_bake_sdf_program;
    dw::gl::Buffer::Ptr  m_global_ubo;
    dw::gl::Buffer::Ptr  m_instance_ubo;
    dw::gl::Buffer::Ptr  m_sdf_ubo;

    std::vector<Instance>       m_instances;
    dw::Mesh::Ptr               m_ground;
    std::unique_ptr<dw::Camera> m_main_camera;

    GlobalUniforms                m_global_uniforms;
    std::vector<InstanceUniforms> m_instance_uniforms;
    std::vector<uint64_t>         m_texture_uniforms;

    // Camera controls.
    bool  m_mouse_look         = false;
    float m_heading_speed      = 0.0f;
    float m_sideways_speed     = 0.0f;
    float m_camera_sensitivity = 0.05f;
    float m_camera_speed       = 0.05f;
    bool  m_debug_gui          = true;

    // Camera orientation.
    float m_camera_x;
    float m_camera_y;

    // Light
    float m_light_pitch = -0.4f;

    // SDF
    float m_t_min               = 0.05f;
    float m_t_max               = 100.0f;
    bool  m_soft_shadows        = true;
    float m_soft_shadows_k      = 4.0f;
    bool  m_draw_bounding_boxes = false;
};

DW_DECLARE_MAIN(SDFShadows)