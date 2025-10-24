#include "rasterizer_renderer.h"

#include "utils/resource_utils.h"
#include "utils/timer.h"
#include <random>
#include <cmath>


void cg::renderer::rasterization_renderer::init()
{
	// Load model and camera from resources
	renderer::load_model();
	renderer::load_camera();
	
	// Create rasterizer instance for vertex and unsigned color data
	rasterizer = std::make_shared<cg::renderer::rasterizer<cg::vertex, cg::unsigned_color>>();
	
	// Set viewport dimensions to match screen resolution
	rasterizer->set_viewport(settings->width, settings->height);
	
	// Create render target texture with the specified dimensions
	render_target = std::make_shared<cg::resource<cg::unsigned_color>>(settings->width, settings->height);
	
	// Create depth buffer for z-buffering
	depth_buffer = std::make_shared<cg::resource<float>>(settings->width, settings->height);
	
	// Attach render target and depth buffer to rasterizer
	rasterizer->set_render_target(render_target, depth_buffer);
}

void cg::renderer::rasterization_renderer::render()
{
	// Compute the combined transformation matrix: projection * view * world
	float4x4 matrix = mul(
		camera->get_projection_matrix(),
		camera->get_view_matrix(),
		model->get_world_matrix()
	);

	// Define vertex shader: transforms vertices using the combined matrix
	rasterizer->vertex_shader = [&](float4 vertex, cg::vertex vertex_data){
		auto processed = mul(matrix, vertex);
		return std::make_pair(processed, vertex_data);
	};

	// Define pixel shader: returns the ambient color of the vertex
	rasterizer->pixel_shader = [](cg::vertex data, float z){
		return cg::color::from_float3(data.ambient);
	};

	// ===== CREATIVE EFFECT: Starfield Background =====
	// Clear render target with dark blue space color
	rasterizer->clear_render_target(cg::unsigned_color{5, 5, 20});

	// Initialize random number generator for procedural star generation
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned> x_dis(0, settings->width - 1);
	std::uniform_int_distribution<unsigned> y_dis(0, settings->height - 1);
	std::uniform_real_distribution<float> brightness_dis(0.4f, 1.0f);
	std::uniform_int_distribution<int> size_dis(0, 2); // Star size: 0-2 pixels for glow
	
	// Calculate number of stars based on screen resolution
	// More pixels = more stars for consistent star density
	int num_stars = (settings->width * settings->height) / 800;
	
	// Generate and render individual stars
	for (int i = 0; i < num_stars; ++i) {
		// Generate random star position and brightness
		unsigned x = x_dis(gen);
		unsigned y = y_dis(gen);
		float brightness = brightness_dis(gen);
		int star_size = size_dis(gen);
		
		// Create star color with slight blue tint
		cg::unsigned_color star_color{
			static_cast<unsigned char>(255 * brightness),
			static_cast<unsigned char>(255 * brightness),
			static_cast<unsigned char>(255 * brightness * 0.95f)
		};
		
		// Draw central pixel of the star
		if (x < settings->width && y < settings->height) {
			render_target->item(x, y) = star_color;
		}
		
		// Add glow effect around bright stars
		if (brightness > 0.7f && star_size > 0) {
			// Render glow halo around bright stars
			for (int dx = -star_size; dx <= star_size; ++dx) {
				for (int dy = -star_size; dy <= star_size; ++dy) {
					if (dx == 0 && dy == 0) continue; // Skip center pixel (already drawn)
					
					int nx = static_cast<int>(x) + dx;
					int ny = static_cast<int>(y) + dy;
					
					// Check bounds to prevent buffer overflow
					if (nx >= 0 && nx < static_cast<int>(settings->width) && 
						ny >= 0 && ny < static_cast<int>(settings->height)) {
						
						// Calculate glow intensity based on distance from star center
						float distance = std::sqrt(dx * dx + dy * dy);
						float glow_intensity = brightness * (1.0f - distance / (star_size + 1));
						
						// Create glow color with reduced intensity
						cg::unsigned_color glow{
							static_cast<unsigned char>(80 * glow_intensity),
							static_cast<unsigned char>(80 * glow_intensity),
							static_cast<unsigned char>(75 * glow_intensity)
						};
						
						// Blend glow with existing background color
						cg::unsigned_color existing = render_target->item(nx, ny);
						render_target->item(nx, ny) = cg::unsigned_color{
							static_cast<unsigned char>(std::min(255, existing.r + glow.r)),
							static_cast<unsigned char>(std::min(255, existing.g + glow.g)),
							static_cast<unsigned char>(std::min(255, existing.b + glow.b))
						};
					}
				}
			}
		}
	}

	// ===== NEBULA EFFECT =====
	// Add procedural nebula clouds for additional visual depth
	std::uniform_real_distribution<float> nebula_color_dis(0.2f, 0.6f);
	int num_nebulas = 3 + (rd() % 3); // Generate 3-5 nebula clouds
	
	for (int n = 0; n < num_nebulas; ++n) {
		// Generate random nebula center position
		unsigned center_x = x_dis(gen);
		unsigned center_y = y_dis(gen);
		unsigned radius = 50 + (rd() % 100); // Nebula radius: 50-150 pixels
		
		// Assign random color to nebula with reduced saturation
		float r_intensity = nebula_color_dis(gen);
		float g_intensity = nebula_color_dis(gen);
		float b_intensity = nebula_color_dis(gen);
		
		// Draw nebula with gradient falloff from center
		for (unsigned y = 0; y < settings->height; ++y) {
			for (unsigned x = 0; x < settings->width; ++x) {
				// Calculate distance from nebula center
				float dx = static_cast<float>(x) - static_cast<float>(center_x);
				float dy = static_cast<float>(y) - static_cast<float>(center_y);
				float distance = std::sqrt(dx * dx + dy * dy);
				
				// Only process pixels within nebula radius
				if (distance < radius) {
					// Intensity decreases towards nebula edges
					float intensity = (1.0f - distance / radius) * 0.3f;
					
					// Create nebula color with calculated intensity
					cg::unsigned_color nebula_color{
						static_cast<unsigned char>(r_intensity * 255 * intensity),
						static_cast<unsigned char>(g_intensity * 255 * intensity),
						static_cast<unsigned char>(b_intensity * 255 * intensity)
					};
					
					// Additive blend nebula with existing background
					cg::unsigned_color existing = render_target->item(x, y);
					render_target->item(x, y) = cg::unsigned_color{
						static_cast<unsigned char>(std::min(255, existing.r + nebula_color.r)),
						static_cast<unsigned char>(std::min(255, existing.g + nebula_color.g)),
						static_cast<unsigned char>(std::min(255, existing.b + nebula_color.b))
					};
				}
			}
		}
	}

	// ===== RENDER 3D MODEL =====
	// Render all mesh shapes of the loaded model over the starfield background
	for(size_t shape_id = 0; shape_id < model->get_index_buffers().size(); shape_id++)
	{
		// Set current shape's vertex and index buffers
		rasterizer->set_vertex_buffer(model->get_vertex_buffers()[shape_id]);
		rasterizer->set_index_buffer(model->get_index_buffers()[shape_id]);
		
		// Draw the shape using its index buffer
		rasterizer->draw(model->get_index_buffers()[shape_id]->count(), 0);
	}
	
	// Save the final rendered image to disk
	cg::utils::save_resource(*render_target, settings->result_path);
}

void cg::renderer::rasterization_renderer::destroy() {}

void cg::renderer::rasterization_renderer::update() {}
