#include <cgv/base/group.h>
#include "gl_context.h"
#include "gl_tools.h"
#include <cgv_gl/gl/wgl.h>
#ifdef _WIN32
#undef TA_LEFT
#undef TA_TOP
#undef TA_RIGHT
#undef TA_BOTTOM
#endif
#include <cgv/base/base.h>
#include <cgv/base/action.h>
#include <cgv/render/drawable.h>
#include <cgv/render/shader_program.h>
#include <cgv/render/attribute_array_binding.h>
#include <cgv/render/textured_material.h>
#include <cgv/utils/scan.h>
#include <cgv/media/image/image_writer.h>
#include <cgv/gui/event_handler.h>
#include <cgv/math/ftransform.h>
#include <cgv/math/geom.h>
#include <cgv/math/inv.h>
#include <cgv/type/standard_types.h>
#include <cgv/os/clipboard.h>

using namespace cgv::base;
using namespace cgv::type;
using namespace cgv::gui;
using namespace cgv::os;
using namespace cgv::math;
using namespace cgv::media::font;
using namespace cgv::media::illum;

namespace cgv {
	namespace render {
		namespace gl {
			
GLuint map_to_gl(PrimitiveType pt)
{
	static GLuint pt_to_gl[] = {
		GLuint (-1),
		GL_POINTS,
		GL_LINES,
		GL_LINES_ADJACENCY,
		GL_LINE_STRIP,
		GL_LINE_STRIP_ADJACENCY,
		GL_LINE_LOOP,
		GL_TRIANGLES,
		GL_TRIANGLES_ADJACENCY,
		GL_TRIANGLE_STRIP,
		GL_TRIANGLE_STRIP_ADJACENCY,
		GL_TRIANGLE_FAN,
		GL_QUADS,
		GL_QUAD_STRIP,
		GL_POLYGON
	};
	return pt_to_gl[pt];
}

GLuint map_to_gl(MaterialSide ms)
{
	static GLuint ms_to_gl[] = {
		0,
		GL_FRONT,
		GL_BACK,
		GL_FRONT_AND_BACK
	};
	return ms_to_gl[ms];
}

GLuint get_gl_id(void* handle)
{
	return (const GLuint&)handle - 1;
}

void* get_handle(GLuint id)
{
	void* handle = 0;
	(GLuint&)handle = id + 1;
	return handle;
}

void gl_context::put_id(void* handle, void* ptr) const
{
	*static_cast<GLuint*>(ptr) = get_gl_id(handle);
}

/// set a very specific texture format. This should be called after the texture is constructed and before it is created.
void set_gl_format(texture& tex, GLuint gl_format, const std::string& component_format_description)
{
	tex.set_component_format(component_format_description);
	(GLuint&)tex.internal_format = gl_format;
}

/// return the texture format used for a given texture. If called before texture has been created, the function returns, which format would be chosen by the automatic format selection process.
GLuint get_gl_format(const texture& tex)
{
	if (tex.internal_format)
		return (const GLuint&)tex.internal_format;
	cgv::data::component_format best_cf;
	GLuint gl_format = find_best_texture_format(tex, &best_cf);
	return gl_format;
}


void gl_set_material_color(GLenum side, const cgv::media::illum::phong_material::color_type& c, float alpha, GLenum type)
{
	GLfloat v[4] = { c[0],c[1],c[2],c[3] * alpha };
	glMaterialfv(side, type, v);
}

/// enable a material without textures
void gl_set_material(const cgv::media::illum::phong_material& mat, MaterialSide ms, float alpha)
{
	if (ms == MS_NONE)
		return;
	unsigned side = map_to_gl(ms);
	gl_set_material_color(side, mat.get_ambient(), alpha, GL_AMBIENT);
	gl_set_material_color(side, mat.get_diffuse(), alpha, GL_DIFFUSE);
	gl_set_material_color(side, mat.get_specular(), alpha, GL_SPECULAR);
	gl_set_material_color(side, mat.get_emission(), alpha, GL_EMISSION);
	glMaterialf(side, GL_SHININESS, mat.get_shininess());
}

/// construct gl_context and attach signals
gl_context::gl_context()
{
	max_nr_indices = 0;
	max_nr_vertices = 0;
	info_font_size = 14;
	// check if a new context has been created or if the size of the viewport has been changed
	font_ptr info_font = find_font("Courier New");
	if (info_font.empty()) {
		info_font = find_font("Courier");
		if (info_font.empty()) {
			info_font = find_font("system");
		}
	}
	if (!info_font.empty())
		info_font_face = info_font->get_font_face(FFA_REGULAR);

	show_help = false;
	show_stats = false;
}

/// return the used rendering API
RenderAPI gl_context::get_render_api() const
{
	return RA_OPENGL;
}

//GL_STACK_OVERFLOW, "stack overflow"
//GL_STACK_UNDERFLOW, "stack underflow",
std::string get_source_tag_name(GLenum tag)
{
	static std::map<GLenum, const char*> source_tags = {
			{ GL_DEBUG_SOURCE_API,             "api" },
			{ GL_DEBUG_SOURCE_WINDOW_SYSTEM,   "window system" },
			{ GL_DEBUG_SOURCE_SHADER_COMPILER, "shader compiler" },
			{ GL_DEBUG_SOURCE_THIRD_PARTY,     "3rd party" },
			{ GL_DEBUG_SOURCE_APPLICATION,     "application" },
			{ GL_DEBUG_SOURCE_OTHER,           "other" }
	};
	return source_tags[tag];
}

std::string get_type_tag_name(GLenum tag)
{
	static std::map<GLenum, const char*> type_tags = {
			{ GL_DEBUG_TYPE_ERROR,               "error" },
			{ GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, "deprecated behavior" },
			{ GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR,  "undefinded behavior" },
			{ GL_DEBUG_TYPE_PORTABILITY,         "portability" },
			{ GL_DEBUG_TYPE_PERFORMANCE,         "performance" },
			{ GL_DEBUG_TYPE_OTHER,               "other" },
			{ GL_DEBUG_TYPE_MARKER,              "marker" },
			{ GL_DEBUG_TYPE_PUSH_GROUP,          "push group" },
			{ GL_DEBUG_TYPE_POP_GROUP,           "pop group" }
	};
	return type_tags[tag];
}

std::string get_severity_tag_name(GLenum tag)
{
	static std::map<GLenum, const char*> severity_tags = {
			{ GL_DEBUG_SEVERITY_NOTIFICATION, "notification" },
			{ GL_DEBUG_SEVERITY_HIGH,         "high" },
			{ GL_DEBUG_SEVERITY_MEDIUM,       "medium" },
			{ GL_DEBUG_SEVERITY_LOW,          "low" }
	};
	return severity_tags[tag];
};

void GLAPIENTRY debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	const gl_context* ctx = reinterpret_cast<const gl_context*>(userParam);
	std::string msg(message, length);
	msg = std::string("GLDebug Message[") + cgv::utils::to_string(id) + "] from " + get_source_tag_name(source) + " of type " + get_type_tag_name(type) + " of severity " + get_severity_tag_name(severity) + "\n" + msg;
	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
		std::cerr << msg << std::endl;
	else
		ctx->error(msg);
}

/// define lighting mode, viewing pyramid and the rendering mode
bool gl_context::configure_gl()
{
	if (!ensure_glew_initialized()) {
		error("gl_context::configure_gl could not initialize glew");
		return false;
	}
#ifdef _DEBUG
	GLint major_version, minor_version, context_flags;
	glGetIntegerv(GL_MAJOR_VERSION, &major_version);
	glGetIntegerv(GL_MINOR_VERSION, &minor_version);
	glGetIntegerv(GL_CONTEXT_FLAGS, &context_flags);
	std::cout << "OpenGL version " << major_version << "." << minor_version << std::endl;
	if ((context_flags & WGL_CONTEXT_DEBUG_BIT_ARB) == 0)
		glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	if (!check_gl_error("gl_context::configure() debug output"))
		glDebugMessageCallback(debug_callback, this);
#endif
	enable_font_face(info_font_face, info_font_size);
	/*
	GLint context_flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &context_flags);
	*/
	// use the eye location to compute the specular lighting
	glLightModelf(GL_LIGHT_MODEL_LOCAL_VIEWER, 0);
	// this makes opengl normalize all surface normals before lighting calculations,
	// which is essential when using scaling to deform tesselated primities
	glEnable(GL_NORMALIZE);
	glViewport(0, 0, get_width(), get_height());
	if (check_gl_error("gl_context::configure_gl before init of children"))
		return false;
	
	group_ptr grp(dynamic_cast<group*>(this));
	single_method_action<cgv::render::drawable, bool, cgv::render::context&> sma(*this, &drawable::init, false, false);
	for (unsigned i = 0; i<grp->get_nr_children(); ++i)
		traverser(sma, "nc").traverse(grp->get_child(i));

	if (check_gl_error("gl_context::configure_gl after init of children."))
		return false;

	return true;
}

void gl_context::resize_gl()
{
	group_ptr grp(dynamic_cast<group*>(this));
	glViewport(0, 0, get_width(), get_height());
	if (grp) {
		single_method_action_2<drawable, void, unsigned int, unsigned int> sma(get_width(), get_height(), &drawable::resize);
		traverser(sma).traverse(grp);
	}
}

/// overwrite function to return info font size in case no font is currently selected
float gl_context::get_current_font_size() const
{
	if (current_font_size == 0)
		return info_font_size;
	return current_font_size;
}
/// overwrite function to return info font face in case no font is currently selected
media::font::font_face_ptr gl_context::get_current_font_face() const
{
	if (current_font_face.empty())
		return info_font_face;
	return current_font_face;
}

void gl_context::init_render_pass()
{
#ifdef WIN32
	wglSwapIntervalEXT(enable_vsynch ? 1 : 0);
#else
	glXSwapIntervalEXT(enable_vsynch ? 1 : 0);
#endif
	if (sRGB_framebuffer)
		glEnable(GL_FRAMEBUFFER_SRGB);
	else
		glDisable(GL_FRAMEBUFFER_SRGB);

//	glPushAttrib(GL_ALL_ATTRIB_BITS);

	if (get_render_pass_flags()&RPF_SET_LIGHTS) {
		for (unsigned i = 0; i < nr_default_light_sources; ++i)
			set_light_source(default_light_source_handles[i], default_light_source[i], false);
	}
	for (unsigned i = 0; i < nr_default_light_sources; ++i)
		if (get_render_pass_flags()&RPF_SET_LIGHTS_ON)
			enable_light_source(default_light_source_handles[i]);
		else
			disable_light_source(default_light_source_handles[i]);

	if (get_render_pass_flags()&RPF_SET_MATERIAL) {
		set_material(default_material);
	}
	if (get_render_pass_flags()&RPF_ENABLE_MATERIAL) {
		// this mode allows to define the ambient and diffuse color of the surface material
		// via the glColor commands
		glEnable(GL_COLOR_MATERIAL);
	}
	if (get_render_pass_flags()&RPF_SET_STATE_FLAGS) {
		// set some default settings
		glEnable(GL_DEPTH_TEST);
		glCullFace(GL_BACK);
		glEnable(GL_CULL_FACE);
		glEnable(GL_NORMALIZE);
	}
	if ((get_render_pass_flags()&RPF_SET_PROJECTION) != 0) {
		set_projection_matrix(cgv::math::perspective4<double>(45.0, (double)get_width()/get_height(),0.001,1000.0));
	}
	glMatrixMode(GL_MODELVIEW);
	if ((get_render_pass_flags()&RPF_SET_MODELVIEW) != 0)
		set_modelview_matrix(cgv::math::look_at4<double>(vec3(0,0,10), vec3(0,0,0), vec3(0,1,0)));
	
	if (check_gl_error("gl_context::init_render_pass before init_frame"))
		return;

	group* grp = dynamic_cast<group*>(this);
	if (grp && (get_render_pass_flags()&RPF_DRAWABLES_INIT_FRAME)) {
		single_method_action<drawable,void,cgv::render::context&> sma(*this, &drawable::init_frame, true, true);
		traverser(sma).traverse(group_ptr(grp));
	}

	if (check_gl_error("gl_context::init_render_pass after init_frame"))
		return;
	// this defines the background color to which the frame buffer is set by glClear
	if (get_render_pass_flags()&RPF_SET_CLEAR_COLOR)
		glClearColor(bg_r,bg_g,bg_b,bg_a);
	// this defines the background color to which the accum buffer is set by glClear
	if (get_render_pass_flags()&RPF_SET_CLEAR_ACCUM)
		glClearAccum(bg_accum_r,bg_accum_g,bg_accum_b,bg_accum_a);
	// this defines the background depth buffer value set by glClear
	if (get_render_pass_flags()&RPF_SET_CLEAR_DEPTH)
		glClearDepth(bg_d);
	// this defines the background depth buffer value set by glClear
	if (get_render_pass_flags()&RPF_SET_CLEAR_STENCIL)
		glClearStencil(bg_s);
	// clear necessary buffers
	GLenum bits = 0;
	if (get_render_pass_flags()&RPF_CLEAR_COLOR)
		bits |= GL_COLOR_BUFFER_BIT;
	if (get_render_pass_flags()&RPF_CLEAR_DEPTH)
		bits |= GL_DEPTH_BUFFER_BIT;
	if (get_render_pass_flags()&RPF_CLEAR_STENCIL)
		bits |= GL_STENCIL_BUFFER_BIT;
	if (get_render_pass_flags()&RPF_CLEAR_ACCUM)
		bits |= GL_ACCUM_BUFFER_BIT;
	if (bits)
		glClear(bits);
}

///
void gl_context::finish_render_pass()
{
//	glPopAttrib();
}

struct format_callback_handler : public traverse_callback_handler
{
	std::ostream& os;
	format_callback_handler(std::ostream& _os) : os(_os) 
	{
	}
	/// called before the children of a group node g are processed, return whether these should be skipped. If children are skipped, the on_leave_children callback is still called.
	bool on_enter_children(group*)
	{
		os << "\a";
		return false;
	}
	/// called when the children of a group node g have been left, return whether to terminate traversal
	bool on_leave_children(group*)
	{
		os << "\b";
		return false;
	}

};

void gl_context::draw_textual_info()
{
	if (show_help || show_stats) {
		glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
		//GLboolean is_lighting;
		//glGetBooleanv(GL_LIGHTING, &is_lighting);
		//if (is_lighting)
		//	glDisable(GL_LIGHTING);
		glDisable(GL_DEPTH_TEST);
		   if (bg_r+bg_g+bg_b < 1.5f)
				glColor4f(1,1,1,1);
			else
				glColor4f(0,0,0,1);

			push_pixel_coords();
			enable_font_face(info_font_face, info_font_size);
			format_callback_handler fch(output_stream());
			set_cursor(20,20);
			group_ptr grp(dynamic_cast<group*>(this));
			if (grp && show_stats) {
				single_method_action<cgv::base::base,void,std::ostream&> sma(output_stream(), &cgv::base::base::stream_stats, false, false);
				traverser(sma,"nc").traverse(grp,&fch);
//				traverser(make_action<std::ostream&>(output_stream(), &base::stream_stats, false),"nc").traverse(base_ptr(this),&fch);
				output_stream() << std::endl;
			}
		    if (bg_r+bg_g+bg_b < 1.5f)
				glColor4f(1,1,0,1);
			else
				glColor4f(0.4f,0.3f,0,1);
			if (grp && show_help) {
				// collect help from myself and all children
				single_method_action<event_handler,void,std::ostream&> sma(output_stream(), &event_handler::stream_help, false, false);
				traverser(sma,"nc").traverse(grp,&fch);
//				traverser(make_action<std::ostream&>(output_stream(), &event_handler::stream_help, false),"nc").traverse(base_ptr(this),&fch);
				output_stream().flush();
			}
			pop_pixel_coords();
		// turn lighting back on for the next frame
		//glEnable(GL_DEPTH_TEST);
		//glEnable(GL_LIGHTING);
		glPopAttrib();
	}
}

void gl_context::perform_screen_shot()
{
	glFlush();
	data::data_view dv;
	if (!read_frame_buffer(dv))
		return;
	std::string ext("bmp");
	std::string exts = cgv::media::image::image_writer::get_supported_extensions();
	if (cgv::utils::is_element("png",exts))
		ext = "png";
	else if (cgv::utils::is_element("tif",exts))
		ext = "tif";
	cgv::media::image::image_writer wr(std::string("screen_shot.")+ext);
	if (wr.is_format_supported(*dv.get_format()))
		wr.write_image(dv);
}

/// get list of program uniforms
void gl_context::enumerate_program_uniforms(shader_program& prog, std::vector<std::string>& names, std::vector<int>* locations_ptr, std::vector<int>* sizes_ptr, std::vector<int>* types_ptr, bool show) const
{
	GLint count;
	glGetProgramiv(get_gl_id(prog.handle), GL_ACTIVE_UNIFORMS, &count);
	for (int i = 0; i < count; ++i) {
		GLchar name[1000];
		GLsizei length;
		GLint size;
		GLenum type;
		glGetActiveUniform(get_gl_id(prog.handle), i, 1000, &length, &size, &type, name);
		std::string name_str(name, length);
		names.push_back(name_str);
		if (sizes_ptr)
			sizes_ptr->push_back(size);
		if (types_ptr)
			types_ptr->push_back(type);
		int loc = glGetUniformLocation(get_gl_id(prog.handle), name_str.c_str());
		if (locations_ptr)
			locations_ptr->push_back(loc);
		if (show)
			std::cout << i << " at " << loc << " = " << name_str << ":" << type << "[" << size << "]" << std::endl;
	}
}

/// set the current color
void gl_context::set_color(const rgba& clr)
{
	if (support_compatibility_mode) {
		glColor4fv(&clr[0]);
	}
	if (shader_program_stack.empty())
		return;

	cgv::render::shader_program& prog = *static_cast<cgv::render::shader_program*>(shader_program_stack.top());
	int clr_loc = prog.get_color_index();
	if (clr_loc == -1)
		return;

	prog.set_attribute(*this, clr_loc, clr);
}

/// set the current material 
void gl_context::set_material(const cgv::media::illum::surface_material& material)
{
	if (support_compatibility_mode) {
		unsigned side = map_to_gl(MS_FRONT_AND_BACK);
		float alpha = 1.0f - material.get_transparency();
		gl_set_material_color(side, material.get_ambient_occlusion()*material.get_diffuse_reflectance(), alpha, GL_AMBIENT);
		gl_set_material_color(side, material.get_diffuse_reflectance(), alpha, GL_DIFFUSE);
		gl_set_material_color(side, material.get_specular_reflectance(), alpha, GL_SPECULAR);
		gl_set_material_color(side, material.get_emission(), alpha, GL_EMISSION);
		glMaterialf(side, GL_SHININESS, 1.0f/(material.get_roughness()+1.0f/128.0f));
	}
	context::set_material(material);
}

/// enable a material with textures
void gl_context::enable_material(textured_material& mat)
{
	set_textured_material(mat);
	mat.enable_textures(*this);	
}

/// disable a material with textures
void gl_context::disable_material(textured_material& mat)
{
	mat.disable_textures(*this);
	current_material_ptr = 0;
	current_material_is_textured = false;
}

/// return a reference to a shader program used to render without illumination
shader_program& gl_context::ref_default_shader_program(bool texture_support)
{
	static shader_program prog;
	static shader_program prog_texture;

	if (!texture_support) {
		if (!prog.is_created()) {
			if (!prog.build_program(*this, "default.glpr")) {
				error("could not build default shader program from default.glpr");
				exit(0);
			}
			prog.specify_standard_uniforms(true, false, false, true);
			prog.specify_standard_vertex_attribute_names(*this, true, false, false);
		}
		return prog;
	}
	if (!prog_texture.is_created()) {
		if (!prog_texture.build_program(*this, "textured_default.glpr")) {
			error("could not build default shader program with texture support from textured_default.glpr");
			exit(0);
		}
		prog_texture.set_uniform(*this, "texture", 0);
		prog_texture.specify_standard_uniforms(true, false, false, true);
		prog_texture.specify_standard_vertex_attribute_names(*this, true, false, true);
	}
	return prog_texture;
}

/// return a reference to the default shader program used to render surfaces without textures
shader_program& gl_context::ref_surface_shader_program(bool texture_support)
{
	static shader_program prog;
	static shader_program prog_texture;

	if (!texture_support) {
		if (!prog.is_created()) {
			if (!prog.build_program(*this, "default_surface.glpr")) {
				error("could not build surface shader program from default_surface.glpr");
				exit(0);
			}
			prog.specify_standard_uniforms(true, true, true, true);
			prog.specify_standard_vertex_attribute_names(*this, true, true, false);
		}
		return prog;
	}
	if (!prog_texture.is_created()) {
		if (!prog_texture.build_program(*this, "textured_surface.glpr")) {
			error("could not build surface shader program with texture support from textured_surface.glpr");
			exit(0);
		}
		prog_texture.specify_standard_uniforms(true, true, true, true);
		prog_texture.specify_standard_vertex_attribute_names(*this, true, true, true);
	}
	return prog_texture;
}

void gl_context::on_lights_changed()
{
	if (support_compatibility_mode) {
		GLint max_nr_lights;
		glGetIntegerv(GL_MAX_LIGHTS, &max_nr_lights);
		for (GLint light_idx = 0; light_idx < max_nr_lights; ++light_idx) {
			if (light_idx >= int(get_nr_enabled_light_sources())) {
				glDisable(GL_LIGHT0 + light_idx);
				continue;
			}
			GLfloat col[4] = { 1,1,1,1 };
			const cgv::media::illum::light_source& light = get_light_source(get_enabled_light_source_handle(light_idx));
			(rgb&)(col[0]) = light.get_emission()*light.get_ambient_scale();
			glLightfv(GL_LIGHT0 + light_idx, GL_AMBIENT, col);
			(rgb&)(col[0]) = light.get_emission();
			glLightfv(GL_LIGHT0 + light_idx, GL_DIFFUSE, col);
			(rgb&)(col[0]) = light.get_emission();
			glLightfv(GL_LIGHT0 + light_idx, GL_SPECULAR, col);

			GLfloat pos[4] = { 0,0,0,light.get_type() == cgv::media::illum::LT_DIRECTIONAL ? 0.0f : 1.0f };
			(vec3&)(pos[0]) = light.get_position();
			glLightfv(GL_LIGHT0 + light_idx, GL_POSITION, pos);
			if (light.get_type() != cgv::media::illum::LT_DIRECTIONAL) {
				glLightf(GL_LIGHT0 + light_idx, GL_CONSTANT_ATTENUATION, light.get_constant_attenuation());
				glLightf(GL_LIGHT0 + light_idx, GL_LINEAR_ATTENUATION, light.get_linear_attenuation());
				glLightf(GL_LIGHT0 + light_idx, GL_QUADRATIC_ATTENUATION, light.get_quadratic_attenuation());
			}
			else {
				glLightf(GL_LIGHT0 + light_idx, GL_CONSTANT_ATTENUATION, 1);
				glLightf(GL_LIGHT0 + light_idx, GL_LINEAR_ATTENUATION, 0);
				glLightf(GL_LIGHT0 + light_idx, GL_QUADRATIC_ATTENUATION, 0);
			}
			if (light.get_type() == cgv::media::illum::LT_SPOT) {
				glLightf(GL_LIGHT0 + light_idx, GL_SPOT_CUTOFF, light.get_spot_cutoff());
				glLightf(GL_LIGHT0 + light_idx, GL_SPOT_EXPONENT, light.get_spot_exponent());
				glLightfv(GL_LIGHT0 + light_idx, GL_SPOT_DIRECTION, light.get_spot_direction());
			}
			else {
				glLightf(GL_LIGHT0 + light_idx, GL_SPOT_CUTOFF, 180.0f);
				glLightf(GL_LIGHT0 + light_idx, GL_SPOT_EXPONENT, 0.0f);
				static float dir[3] = { 0,0,1 };
				glLightfv(GL_LIGHT0 + light_idx, GL_SPOT_DIRECTION, dir);
			}
			glEnable(GL_LIGHT0 + light_idx);
		}
	}
	context::on_lights_changed();
}

void gl_context::tesselate_arrow(double length, double aspect, double rel_tip_radius, double tip_aspect, int res, bool edges)
{
	double cyl_radius = length*aspect;
	double cone_radius = rel_tip_radius*cyl_radius;
	double cone_length = cone_radius/tip_aspect;
	double cyl_length = length - cone_length;
	push_modelview_matrix();
	mul_modelview_matrix(cgv::math::scale4(cyl_radius,cyl_radius,0.5*cyl_length));
		push_modelview_matrix();
			mul_modelview_matrix(cgv::math::rotate4(180.0,1.0,0.0,0.0));
			tesselate_unit_disk(res, false, edges);
		pop_modelview_matrix();

		mul_modelview_matrix(cgv::math::translate4(0.0,0.0,1.0));
		tesselate_unit_cylinder(res, false, edges);

	mul_modelview_matrix(cgv::math::translate4(0.0, 0.0, 1.0));
	mul_modelview_matrix(cgv::math::scale4(rel_tip_radius, rel_tip_radius, cone_length / cyl_length));
		push_modelview_matrix();
			mul_modelview_matrix(cgv::math::rotate4(180.0, 1.0, 0.0, 0.0));
				tesselate_unit_disk(res, false, edges);
		pop_modelview_matrix();
	mul_modelview_matrix(cgv::math::translate4(0.0, 0.0, 1.0));
		tesselate_unit_cone(res, false, edges);
	pop_modelview_matrix();
}

/// helper function that multiplies a rotation to modelview matrix such that vector is rotated onto target
void gl_context::rotate_vector_to_target(const dvec3& vector, const dvec3& target)
{
	double angle;
	dvec3 axis;
	compute_rotation_axis_and_angle_from_vector_pair(vector, target, axis, angle);
	mul_modelview_matrix(cgv::math::rotate4<double>(180.0 / M_PI * angle, axis));
}

///
void gl_context::tesselate_arrow(const dvec3& start, const dvec3& end, double aspect, double rel_tip_radius, double tip_aspect, int res, bool edges)
{
	if ((start - end).length() < 1e-8) {
		error("ignored tesselate arrow called with start and end closer then 1e-8");
		return;
	}
	push_modelview_matrix();
		mul_modelview_matrix(cgv::math::translate4<double>(start));
		rotate_vector_to_target(dvec3(0, 0, 1), end - start);
		tesselate_arrow((end-start).length(), aspect, rel_tip_radius, tip_aspect, res, edges);
	pop_modelview_matrix();
}

void gl_context::draw_light_source(const light_source& l, float i, float light_scale)
{	
	set_color(i*l.get_emission());
	push_modelview_matrix();
	switch (l.get_type()) {
	case LT_DIRECTIONAL :
		mul_modelview_matrix(cgv::math::scale4<double>(light_scale, light_scale, light_scale));
		tesselate_arrow(vec3(0.0f), l.get_position(), 0.1f,2.0f,0.5f);
		break;
	case LT_POINT :
		mul_modelview_matrix(
			cgv::math::translate4<double>(l.get_position())*
			cgv::math::scale4<double>(vec3(0.3f*light_scale)));
		tesselate_unit_sphere();
		break;
	case LT_SPOT :
		mul_modelview_matrix(
			cgv::math::translate4<double>(l.get_position())*
			cgv::math::scale4<double>(vec3(light_scale))
		);
		rotate_vector_to_target(dvec3(0, 0, -1), l.get_spot_direction());
		{
			float t = tan(l.get_spot_cutoff()*(float)M_PI/180);
			if (l.get_spot_cutoff() > 45.0f)
				mul_modelview_matrix(cgv::math::scale4<double>(1, 1, 0.5f / t));
			else
				mul_modelview_matrix(cgv::math::scale4<double>(t, t, 0.5f));
			mul_modelview_matrix(cgv::math::translate4<double>(0, 0, -1));
			GLboolean cull = glIsEnabled(GL_CULL_FACE);
			glDisable(GL_CULL_FACE);
			tesselate_unit_cone();
			if (cull)
				glEnable(GL_CULL_FACE);
		}
	}
	pop_modelview_matrix();
}


/// use this to push transformation matrices on the stack such that x and y coordinates correspond to window coordinates
void gl_context::push_pixel_coords()
{
	push_projection_matrix();
	push_modelview_matrix();

	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);

	if (support_compatibility_mode) {
		// push projection matrix
		glMatrixMode(GL_PROJECTION);
		// set orthogonal projection
		glLoadIdentity();
		gluOrtho2D(0, vp[2], vp[3], 0);
		// push modelview matrix
		glMatrixMode(GL_MODELVIEW);
		// use identity for modelview
		glLoadIdentity();
	}
	set_modelview_matrix(cgv::math::identity4<double>());
	set_projection_matrix(cgv::math::ortho4<double>(0, vp[2], vp[3], 0, -1, 1));
}

/// pop previously changed transformation matrices 
void gl_context::pop_pixel_coords()
{
	pop_modelview_matrix();
	pop_projection_matrix();
}

/// implement according to specification in context class
bool gl_context::read_frame_buffer(data::data_view& dv, 
											  unsigned int x, unsigned int y, FrameBufferType buffer_type, 
											  TypeId type, data::ComponentFormat cf, int w, int h)
{
	const cgv::data::data_format* df = dv.get_format();
	if (df) {
		w = df->get_width();
		h = df->get_height();
		type = df->get_component_type();
		cf = df->get_standard_component_format();
		if (w < 1 || h < 1) {
			error(std::string("read_frame_buffer: received invalid dimensions (") + cgv::utils::to_string(w) + "," + cgv::utils::to_string(h) + ")");
			return false;
		}
	}
	else {
		if (w < 1 || h < 1) {
			GLint vp[4];
			glGetIntegerv(GL_VIEWPORT,vp);
			w = w < 1 ? vp[2] : w;
			h = h < 1 ? vp[3] : h;
		}
	}
	GLuint gl_type = map_to_gl(type);
	if (gl_type == 0) {
		error(std::string("read_frame_buffer: could not make component type ")+cgv::type::info::get_type_name(df->get_component_type())+" to gl type");
		return false;
	}
	GLuint gl_format = GL_DEPTH_COMPONENT;
	if (cf != cgv::data::CF_D) {
		gl_format = GL_STENCIL_INDEX;
		if (cf != cgv::data::CF_S) {
			gl_format = map_to_gl(cf);
			if (gl_format == GL_RGB && cf != cgv::data::CF_RGB) {
				error(std::string("read_frame_buffer: could not match component format ") + cgv::utils::to_string(df->get_component_format()));
				return false;
			}
		}
	}
	GLenum gl_buffer = GL_BACK;
	if (buffer_type < FB_BACK)
		gl_buffer = GL_COLOR_ATTACHMENT0+buffer_type;
	else {
		switch (buffer_type) {
		case FB_FRONT :       gl_buffer = GL_FRONT; break;
		case FB_BACK  :       gl_buffer = GL_BACK; break;
		case FB_FRONT_LEFT :  gl_buffer = GL_FRONT_LEFT; break;
		case FB_FRONT_RIGHT : gl_buffer = GL_FRONT_RIGHT; break;
		case FB_BACK_LEFT  :  gl_buffer = GL_BACK_LEFT; break;
		case FB_BACK_RIGHT  : gl_buffer = GL_BACK_RIGHT; break;
		default:
			error(std::string("invalid buffer type ")+cgv::utils::to_string(buffer_type));
			return false;
		}
	}
	// after all necessary information could be extracted, ensure that format 
	// and data view are created
	if (!df) {
		df = new cgv::data::data_format(w,h,type,cf);
		dv.~data_view();
		new(&dv) data::data_view(df);
		dv.manage_format(true);
	}
	GLint old_buffer, old_pack;
	glGetIntegerv(GL_READ_BUFFER, &old_buffer);
	glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack);
	glReadBuffer(gl_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, df->get_component_alignment());
	glReadPixels(x,y,w,h,gl_format,gl_type,dv.get_ptr<void>());
	glReadBuffer(old_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, old_pack);
	dv.reflect_horizontally();
	return true;
}


void render_vertex(int k, const float* vertices, const float* normals, const float* tex_coords,
						  const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices, bool flip_normals)
{
	if (normals && normal_indices) {
		if (flip_normals) {
			float n[3] = { -normals[3*normal_indices[k]],-normals[3*normal_indices[k]+1],-normals[3*normal_indices[k]+2] };
			glNormal3fv(n);
		}
		else
			glNormal3fv(normals+3*normal_indices[k]);
	}
	if (tex_coords && tex_coord_indices)
		glTexCoord2fv(tex_coords+2*tex_coord_indices[k]);
	glVertex3fv(vertices+3*vertex_indices[k]);
}

bool gl_context::release_attributes(const float* normals, const float* tex_coords, const int* normal_indices, const int* tex_coord_indices) const
{
	shader_program* prog_ptr = static_cast<shader_program*>(get_current_program());
	if (!prog_ptr || prog_ptr->get_position_index() == -1)
		return false;
	attribute_array_binding::disable_global_array(*this, prog_ptr->get_position_index());
	if (prog_ptr->get_normal_index() != -1 && normals && normal_indices)
		attribute_array_binding::disable_global_array(*this, prog_ptr->get_normal_index());
	if (prog_ptr->get_texcoord_index() != -1 && tex_coords && tex_coord_indices)
		attribute_array_binding::disable_global_array(*this, prog_ptr->get_texcoord_index());
	return true;
}

bool gl_context::prepare_attributes(std::vector<vec3>& P, std::vector<vec3>& N, std::vector<vec2>& T, unsigned nr_vertices,
	const float* vertices, const float* normals, const float* tex_coords,
	const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices, bool flip_normals) const
{
	unsigned i;
	shader_program* prog_ptr = static_cast<shader_program*>(get_current_program());
	if (!prog_ptr || prog_ptr->get_position_index() == -1)
		return false;
	P.resize(nr_vertices);
	for (i = 0; i < nr_vertices; ++i)
		P[i] = *reinterpret_cast<const vec3*>(vertices + 3 * vertex_indices[i]);
	attribute_array_binding::set_global_attribute_array<>(*this, prog_ptr->get_position_index(), P);
	attribute_array_binding::enable_global_array(*this, prog_ptr->get_position_index());
	if (prog_ptr->get_normal_index() != -1) {
		if (normals && normal_indices) {
			N.resize(nr_vertices);
			for (i = 0; i < nr_vertices; ++i) {
				N[i] = *reinterpret_cast<const vec3*>(normals + 3 * normal_indices[i]);
				if (flip_normals)
					N[i] = -N[i];
			}
			attribute_array_binding::set_global_attribute_array<>(*this, prog_ptr->get_normal_index(), N);
			attribute_array_binding::enable_global_array(*this, prog_ptr->get_normal_index());
		}
		else
			attribute_array_binding::disable_global_array(*this, prog_ptr->get_normal_index());
	}
	if (prog_ptr->get_texcoord_index() != -1) {
		if (tex_coords && tex_coord_indices) {
			T.resize(nr_vertices);
			for (i = 0; i < nr_vertices; ++i)
				T[i] = *reinterpret_cast<const vec2*>(tex_coords + 2 * tex_coord_indices[i]);
			attribute_array_binding::set_global_attribute_array<>(*this, prog_ptr->get_texcoord_index(), T);
			attribute_array_binding::enable_global_array(*this, prog_ptr->get_texcoord_index());
		}
		else
			attribute_array_binding::disable_global_array(*this, prog_ptr->get_texcoord_index());
	}
	return true;
}

/// pass geometry of given faces to current shader program and generate draw calls to render lines for the edges
void gl_context::draw_edges_of_faces(
	const float* vertices, const float* normals, const float* tex_coords,
	const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices,
	int nr_faces, int face_degree, bool flip_normals) const
{
	if (draw_in_compatibility_mode) {
		int k = 0;
		for (int i = 0; i < nr_faces; ++i) {
			glBegin(GL_LINE_LOOP);
			for (int j = 0; j < face_degree; ++j, ++k)
				render_vertex(k, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
			glEnd();
		}
		return;
	}
	unsigned nr_vertices = face_degree * nr_faces;
	std::vector<vec3> P, N;
	std::vector<vec2> T;
	if (!prepare_attributes(P, N, T, nr_vertices, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals))
		return;
	for (int i = 0; i < nr_faces; ++i)
		glDrawArrays(GL_LINE_LOOP, i*face_degree, face_degree);
	release_attributes(normals, tex_coords, normal_indices, tex_coord_indices);
}


void gl_context::draw_elements_void(GLenum mode, size_t total_count, GLenum type, size_t type_size, const void* indices) const
{
	ensure_configured();
	size_t drawn = 0;
	const cgv::type::uint8_type* index_ptr = static_cast<const cgv::type::uint8_type*>(indices);
	while (drawn < total_count) {
		size_t count = total_count - drawn;
		if (count > max_nr_indices)
			count = size_t(max_nr_indices);
		glDrawElements(mode, GLsizei(count), type, index_ptr + drawn * type_size);
		drawn += count;
	}
}

size_t max_nr_indices, max_nr_vertices;
void gl_context::ensure_configured() const
{
	if (max_nr_indices != 0)
		return;
	glGetInteger64v(GL_MAX_ELEMENTS_INDICES, reinterpret_cast<GLint64*>(&max_nr_indices));
	glGetInteger64v(GL_MAX_ELEMENTS_VERTICES, reinterpret_cast<GLint64*>(&max_nr_vertices));
}

/// pass geometry of given strip or fan to current shader program and generate draw calls to render lines for the edges
void gl_context::draw_edges_of_strip_or_fan(
	const float* vertices, const float* normals, const float* tex_coords,
	const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices,
	int nr_faces, int face_degree, bool is_fan, bool flip_normals) const
{
	int s = face_degree - 2;
	int i, k = 2;
	std::vector<GLuint> I;
	I.push_back(0);	I.push_back(1);
	for (i = 0; i < nr_faces; ++i) {
		if (is_fan) {
			I.push_back(k - 1);	I.push_back(k);
			I.push_back(0);	I.push_back(k);
			continue;
		}
		if (s == 1) {
			I.push_back(k - 1);	I.push_back(k);
			I.push_back(k - 2);	I.push_back(k);
		}
		else {
			I.push_back(k - 1);	I.push_back(k + 1);
			I.push_back(k - 2);	I.push_back(k);
			I.push_back(k);	I.push_back(k + 1);
		}
		k += 2;
	}

	if (draw_in_compatibility_mode) {
		glBegin(GL_LINES);
		for (GLuint j:I)
			render_vertex(j, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
		glEnd();
		return;
	}
	unsigned nr_vertices = 2 + (face_degree - 2) * nr_faces;
	std::vector<vec3> P, N;
	std::vector<vec2> T;
	if (!prepare_attributes(P, N, T, nr_vertices, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals))
		return;
	draw_elements(GL_LINES, I.size(), &I[0]);
	release_attributes(normals, tex_coords, normal_indices, tex_coord_indices);
}

void gl_context::draw_faces(
	const float* vertices, const float* normals, const float* tex_coords, 
	const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices, 
	int nr_faces, int face_degree, bool flip_normals) const
{
	if (draw_in_compatibility_mode) {
		int k = 0;
		if (face_degree < 5)
			glBegin(face_degree == 3 ? GL_TRIANGLES : GL_QUADS);
		for (int i = 0; i < nr_faces; ++i) {
			if (face_degree >= 5)
				glBegin(GL_POLYGON);
			for (int j = 0; j < face_degree; ++j, ++k)
				render_vertex(k, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
			if (face_degree >= 5)
				glEnd();
		}
		if (face_degree < 5)
			glEnd();
		return;
	}
	unsigned nr_vertices = face_degree * nr_faces;
	std::vector<vec3> P, N;
	std::vector<vec2> T;
	if (!prepare_attributes(P, N, T, nr_vertices, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals))
		return;
	/*
	for (unsigned x = 0; x < nr_vertices; ++x) {
		std::cout << x << ": [" << P[x] << "]";
		if (N.size() > 0)
			std::cout << ", <" << N[x] << ">";
		if (T.size() > 0) 
			std::cout << ", {" << T[x] << "}";
		std::cout << std::endl;
	}
	*/
	if (face_degree == 3)
		glDrawArrays(GL_TRIANGLES, 0, nr_vertices);
	else {
		for (int i = 0; i < nr_faces; ++i) {
			glDrawArrays(GL_TRIANGLE_FAN, i*face_degree, face_degree);
		}
	}
	release_attributes(normals, tex_coords, normal_indices, tex_coord_indices);
}

void gl_context::draw_strip_or_fan(
		const float* vertices, const float* normals, const float* tex_coords, 
		const int* vertex_indices, const int* normal_indices, const int* tex_coord_indices, 
		int nr_faces, int face_degree, bool is_fan, bool flip_normals) const
{
	int s = face_degree - 2;
	int k = 2;
	if (draw_in_compatibility_mode) {
		glBegin(face_degree == 3 ? (is_fan ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP) : GL_QUAD_STRIP);
		render_vertex(0, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
		render_vertex(1, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
		for (int i = 0; i < nr_faces; ++i)
			for (int j = 0; j < s; ++j, ++k)
				render_vertex(k, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals);
		glEnd();
		return;
	}
	unsigned nr_vertices = 2 + (face_degree-2) * nr_faces;
	std::vector<vec3> P, N;
	std::vector<vec2> T;
	if (!prepare_attributes(P, N, T, nr_vertices, vertices, normals, tex_coords, vertex_indices, normal_indices, tex_coord_indices, flip_normals))
		return;
	glDrawArrays(is_fan ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP, 0, nr_vertices);
	release_attributes(normals, tex_coords, normal_indices, tex_coord_indices);
}

/// return homogeneous 4x4 viewing matrix, which transforms from world to eye space
gl_context::dmat4 gl_context::get_modelview_matrix() const
{
	if (support_compatibility_mode) {
		GLdouble V[16];
		glGetDoublev(GL_MODELVIEW_MATRIX, V);
		return dmat4(4,4,V);
	}
	if (modelview_matrix_stack.empty())
		return identity4<double>();
	return modelview_matrix_stack.top();
}

/// return homogeneous 4x4 projection matrix, which transforms from eye to clip space
gl_context::dmat4 gl_context::get_projection_matrix() const
{
	if (support_compatibility_mode) {
		GLdouble P[16];
		glGetDoublev(GL_PROJECTION_MATRIX, P);
		return dmat4(4,4,P);
	}
	if (projection_matrix_stack.empty())
		return identity4<double>();
	return projection_matrix_stack.top();
}

/// return homogeneous 4x4 projection matrix, which transforms from clip to device space
gl_context::dmat4 gl_context::get_device_matrix() const
{
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	dmat4 D; D.zeros();
	D(0, 0) = 0.5*vp[2];
	D(0, 3) = 0.5*vp[2] + vp[0];
	D(1, 1) = -0.5*vp[3]; // flip y-coordinate
	D(1, 3) = get_height() - 0.5*vp[3] - vp[1];
	D(2, 2) = 0.5;
	D(2, 3) = 0.5;
	D(3, 3) = 1.0;
	return D;
}

void gl_context::set_modelview_matrix(const dmat4& V)
{
	if (support_compatibility_mode) {
		GLint mm;
		glGetIntegerv(GL_MATRIX_MODE, &mm);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixd(V);
		glMatrixMode(mm);
	}
	context::set_modelview_matrix(V);
}

void gl_context::set_projection_matrix(const dmat4& P)
{
	if (support_compatibility_mode) {
		GLint mm;
		glGetIntegerv(GL_MATRIX_MODE, &mm);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixd(P);
		glMatrixMode(mm);
	}
	context::set_projection_matrix(P);
}

/// read the device z-coordinate from the z-buffer for the given device x- and y-coordinates
double gl_context::get_z_D(int x_D, int y_D) const
{
	GLfloat z_D;

	if (!in_render_process() && !is_current())
		make_current();

	glReadPixels(x_D, get_height()-y_D-1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z_D);
	/*
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		std::cout << "gl error:";
		switch (err) {
			case GL_INVALID_ENUM :      std::cout << "invalid enum"; break;
			case GL_INVALID_VALUE :     std::cout << "invalid value"; break;
			case GL_INVALID_OPERATION : std::cout << "invalid operation"; break;
			case GL_STACK_OVERFLOW :    std::cout << "stack overflow"; break;
			case GL_STACK_UNDERFLOW :   std::cout << "stack underflow"; break;
			case GL_OUT_OF_MEMORY :     std::cout << "out of memory"; break;
			default:                    std::cout << "unknown error"; break;
		}
		std::cout << std::endl;

	}
	*/
	return z_D;
}
static const GLenum gl_depth_format_ids[] = 
{
	GL_DEPTH_COMPONENT,
	GL_DEPTH_COMPONENT16,
	GL_DEPTH_COMPONENT24,
	GL_DEPTH_COMPONENT32
};

static const GLenum gl_color_buffer_format_ids[] = 
{
	GL_RGB,
	GL_RGBA
};

static const char* depth_formats[] = 
{
	"[D]",
	"uint16[D]",
	"uint32[D:24]",
	"uint32[D]",
	0
};

static const char* color_buffer_formats[] = 
{
	"[R,G,B]",
	"[R,G,B,A]",
	0
};


GLuint get_tex_dim(TextureType tt) {
	static GLuint tex_dim[] = { 0, GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP };
	return tex_dim[tt];
}

GLuint get_tex_bind(TextureType tt) {
	static GLuint tex_bind[] = { 0, GL_TEXTURE_BINDING_1D, GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_3D, GL_TEXTURE_BINDING_CUBE_MAP, GL_TEXTURE_BUFFER };
	return tex_bind[tt];
}


unsigned int map_to_gl(TextureWrap wrap)
{
	static const GLenum gl_texture_wrap[] = { 
		GL_REPEAT, 
		GL_CLAMP, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, 
		GL_MIRROR_CLAMP_EXT, GL_MIRROR_CLAMP_TO_EDGE_EXT, GL_MIRROR_CLAMP_TO_BORDER_EXT, GL_MIRRORED_REPEAT
	};
	return gl_texture_wrap[wrap];
}

unsigned int map_to_gl(TextureFilter filter_type)
{
	static const GLenum gl_texture_filter[] = {
		GL_NEAREST, GL_LINEAR,
		GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
		GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR,
		GL_TEXTURE_MAX_ANISOTROPY_EXT
	};
	return gl_texture_filter[filter_type];
}

unsigned int map_to_gl(CompareFunction cf)
{
	static const GLenum gl_cfs[] = {
		GL_LEQUAL,
		GL_GEQUAL,
		GL_LESS,
		GL_GREATER,
		GL_EQUAL,
		GL_NOTEQUAL,
		GL_ALWAYS,
		GL_NEVER
	};
	return gl_cfs[cf];
}

cgv::data::component_format gl_context::texture_find_best_format(
				const cgv::data::component_format& cf, 
				render_component& rc, const std::vector<cgv::data::data_view>* palettes) const
{
	GLuint& gl_format = (GLuint&)rc.internal_format;
	cgv::data::component_format best_cf;
	gl_format = find_best_texture_format(cf, &best_cf, palettes);
	return best_cf;
}

std::string gl_error() {
	GLenum eid = glGetError();
	return std::string((const char*)gluErrorString(eid));
}

bool gl_context::check_gl_error(const std::string& where, const cgv::render::render_component* rc) const
{
	GLenum eid = glGetError();
	if (eid == GL_NO_ERROR)
		return false;
	std::string error_string = where + ": " + std::string((const char*)gluErrorString(eid));
	error(error_string, rc);
	return true;
}

bool gl_context::check_texture_support(TextureType tt, const std::string& where, const cgv::render::render_component* rc) const
{
	switch (tt) {
	case TT_3D:
		if (!GLEW_VERSION_1_2) {
			error(where + ": 3D texture not supported", rc);
			return false;
		}
		break;
	case TT_CUBEMAP:
		if (!GLEW_VERSION_1_3) {
			error(where + ": cubemap texture not supported", rc);
			return false;
		}
		break;
	}
	return true;
}

bool gl_context::check_shader_support(ShaderType st, const std::string& where, const cgv::render::render_component* rc) const
{
	switch (st) {
	case ST_COMPUTE:
		if (GLEW_VERSION_4_3)
			return true;
		else {
			error(where+": compute shader need not supported OpenGL version 4.3", rc);
			return false;
		}
	case ST_TESS_CONTROL:
	case ST_TESS_EVALUTION:
		if (GLEW_VERSION_4_0)
			return true;
		else {
			error(where+": tesselation shader need not supported OpenGL version 4.0", rc);
			return false;
		}
	case ST_GEOMETRY:
		if (GLEW_VERSION_3_2)
			return true;
		else {
			error(where + ": geometry shader need not supported OpenGL version 3.2", rc);
			return false;
		}
	default:
		if (GLEW_VERSION_2_0)
			return true;
		else {
			error(where + ": shaders need not supported OpenGL version 2.0", rc);
			return false;
		}
	}
}

bool gl_context::check_fbo_support(const std::string& where, const cgv::render::render_component* rc) const
{
	if (!GLEW_VERSION_3_0) {
		error(where + ": framebuffer objects not supported", rc);
		return false;
	}
	return true;
}

GLuint gl_context::texture_generate(texture_base& tb) const
{
	if (!check_texture_support(tb.tt, "gl_context::texture_generate", &tb))
		return get_gl_id(0);
	GLuint tex_id = get_gl_id(0);
	glGenTextures(1, &tex_id);
	if (glGetError() == GL_INVALID_OPERATION)
		error("gl_context::texture_generate: attempt to create texture inside glBegin-glEnd-block", &tb);
	return tex_id;
}

int gl_context::query_integer_constant(ContextIntegerConstant cic) const
{
	GLint gl_const;
	switch (cic) {
		case MAX_NR_GEOMETRY_SHADER_OUTPUT_VERTICES :
			gl_const = GL_MAX_GEOMETRY_OUTPUT_VERTICES;
			break;
	}
	GLint value;
	glGetIntegerv(gl_const, &value);
	return value;
}

GLuint gl_context::texture_bind(TextureType tt, GLuint tex_id) const
{
	GLint tmp_id;
	glGetIntegerv(get_tex_bind(tt), &tmp_id);
	glBindTexture(get_tex_dim(tt), tex_id);
	return tmp_id;
}

void gl_context::texture_unbind(TextureType tt, GLuint tmp_id) const
{
	glBindTexture(get_tex_dim(tt), tmp_id);
}

bool gl_context::texture_create(texture_base& tb, cgv::data::data_format& df) const
{
	GLuint gl_format = (const GLuint&) tb.internal_format;
	
	if (tb.tt == TT_UNDEF)
		tb.tt = (TextureType)df.get_nr_dimensions();
	GLuint tex_id = texture_generate(tb);
	if (tex_id == get_gl_id(0))
		return false;
	GLuint tmp_id = texture_bind(tb.tt, tex_id);

	switch (tb.tt) {
	case TT_1D :
		glTexImage1D(GL_TEXTURE_1D, 0, 
			gl_format, df.get_width(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		break;
	case TT_2D :
		{
			unsigned int comp_type = GL_RGBA;
			if (std::string(df.get_component_name(0)) == "D")
				comp_type = GL_DEPTH_COMPONENT;
			glTexImage2D(GL_TEXTURE_2D, 0, 
				gl_format, df.get_width(), df.get_height(), 0, comp_type, GL_UNSIGNED_BYTE, 0);
			break;
		}
	case TT_3D :
		glTexImage3D(GL_TEXTURE_3D, 0,
			gl_format, df.get_width(), df.get_height(), df.get_depth(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		break;
	case TT_CUBEMAP :
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D( GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D( GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexImage2D( GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0,
			gl_format, df.get_width(), df.get_height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		break;
	}
	if (check_gl_error("gl_context::texture_create", &tb)) {
		glDeleteTextures(1, &tex_id);
		texture_unbind(tb.tt, tmp_id);
		return false;
	}

	texture_unbind(tb.tt, tmp_id);
	tb.have_mipmaps = false;
	tb.handle = get_handle(tex_id);
	return true;
}

bool gl_context::texture_create(
							texture_base& tb, 
							cgv::data::data_format& target_format, 
							const cgv::data::const_data_view& data, 
							int level, int cube_side, const std::vector<cgv::data::data_view>* palettes) const
{
	// query the format to be used for the texture
	GLuint gl_tex_format = (const GLuint&) tb.internal_format;

	// define the texture type from the data format and the cube_side parameter
	tb.tt = (TextureType)data.get_format()->get_nr_dimensions();
	if (tb.tt == TT_2D && cube_side != -1)
		tb.tt = TT_CUBEMAP;
	
	// create texture is not yet done
	GLuint tex_id;
	if (tb.is_created()) 
		tex_id = get_gl_id(tb.handle);
	else {
		tex_id = texture_generate(tb);
		if (tex_id == get_gl_id(0))
			return false;
		tb.handle = get_handle(tex_id);
	}

	// bind texture
	GLuint tmp_id = texture_bind(tb.tt, tex_id);

	// load data to texture
	tb.have_mipmaps = load_texture(data, gl_tex_format, level, cube_side, palettes);
	bool result = !check_gl_error("gl_context::texture_create", &tb);
	// restore old texture
	texture_unbind(tb.tt, tmp_id);
	return result;
}

bool gl_context::texture_create_from_buffer(
						texture_base& tb, 
						cgv::data::data_format& df, 
						int x, int y, int level) const
{
	GLuint gl_format = (const GLuint&) tb.internal_format;

	tb.tt = (TextureType)df.get_nr_dimensions();
	if (tb.tt != TT_2D) {
		tb.last_error = "texture creation from buffer only possible for 2d textures";
		return false;
	}
	GLuint tex_id;
	if (tb.is_created()) 
		tex_id = get_gl_id(tb.handle);
	else {
		tex_id = texture_generate(tb);
		if (tex_id == get_gl_id(0))
			return false;
		tb.handle = get_handle(tex_id);
	}
	GLuint tmp_id = texture_bind(tb.tt, tex_id);

	// check mipmap type
	bool gen_mipmap = level == -1;
	if (gen_mipmap)
		level = 0;

	glCopyTexImage2D(GL_TEXTURE_2D, level, gl_format, x, y, df.get_width(), df.get_height(), 0);
	bool result = false;
	std::string error_string("gl_context::texture_create_from_buffer: ");
	switch (glGetError()) {
	case GL_NO_ERROR :
		result = true;
		break;
	case GL_INVALID_ENUM : 
		error_string += "target was not an accepted value."; 
		break;
	case GL_INVALID_VALUE : 
		error_string += "level was less than zero or greater than log sub 2(max), where max is the returned value of GL_MAX_TEXTURE_SIZE.\n"
							 "or border was not zero or 1.\n"
							 "or width was less than zero, greater than 2 + GL_MAX_TEXTURE_SIZE; or width cannot be represented as 2n + 2 * border for some integer n.";
		break;
	case GL_INVALID_OPERATION : 
		error_string += "glCopyTexImage2D was called between a call to glBegin and the corresponding call to glEnd.";
		break;
	default:
		error_string += (const char*)gluErrorString(glGetError());
		break;
	}
	texture_unbind(tb.tt, tmp_id);
	if (!result)
		error(error_string, &tb);
	else
		if (gen_mipmap)
			result = texture_generate_mipmaps(tb, tb.tt == TT_CUBEMAP ? 2 : (int)tb.tt);

	return result;
}

bool gl_context::texture_replace(
						texture_base& tb, 
						int x, int y, int z, 
						const cgv::data::const_data_view& data, 
						int level, const std::vector<cgv::data::data_view>* palettes) const
{
	if (!tb.is_created()) {
		error("gl_context::texture_replace: attempt to replace in not created texture", &tb);
		return false;
	}
	// determine dimension from location arguments
	unsigned int dim = 1;
	if (y != -1) {
		++dim;
		if (z != -1)
			++dim;
	}
	// check consistency
	if (tb.tt == TT_CUBEMAP) {
		if (dim != 3) {
			error("gl_context::texture_replace: replace on cubemap without the side defined", &tb);
			return false;
		}
		if (z < 0 || z > 5) {
			error("gl_context::texture_replace: replace on cubemap without invalid side specification", &tb);
			return false;
		}
	}
	else {
		if (tb.tt != dim) {
			error("gl_context::texture_replace: replace on texture with invalid position specification", &tb);
			return false;
		}
	}

	// bind texture
	GLuint tmp_id = texture_bind(tb.tt, get_gl_id(tb.handle));
	tb.have_mipmaps = replace_texture(data, level, x, y, z, palettes) || tb.have_mipmaps;
	bool result = !check_gl_error("gl_context::texture_replace", &tb);
	texture_unbind(tb.tt, tmp_id);
	return result;
}

bool gl_context::texture_replace_from_buffer(
							texture_base& tb, 
							int x, int y, int z, 
							int x_buffer, int y_buffer, 
							unsigned int width, unsigned int height, 
							int level) const
{
	if (!tb.is_created()) {
		error("gl_context::texture_replace_from_buffer: attempt to replace in not created texture", &tb);
		return false;
	}
	// determine dimension from location arguments
	unsigned int dim = 2;
	if (z != -1)
		++dim;

	// consistency checks
	if (tb.tt == TT_CUBEMAP) {
		if (dim != 3) {
			error("gl_context::texture_replace_from_buffer: replace on cubemap without the side defined", &tb);
			return false;
		}
		if (z < 0 || z > 5) {
			error("gl_context::texture_replace_from_buffer: replace on cubemap without invalid side specification", &tb);
			return false;
		}
	}
	else {
		if (tb.tt != dim) {
			tb.last_error = "replace on texture with invalid position specification";
			return false;
		}
	}
	// check mipmap type
	bool gen_mipmap = level == -1;
	if (gen_mipmap)
		level = 0;

	// bind texture
	GLuint tmp_id = texture_bind(tb.tt, get_gl_id(tb.handle));
	switch (tb.tt) {
	case TT_2D :      glCopyTexSubImage2D(GL_TEXTURE_2D, level, x, y, x_buffer, y_buffer, width, height); break;
	case TT_3D :      glCopyTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, x_buffer, y_buffer, width, height); break;
	case TT_CUBEMAP : glCopyTexSubImage2D(get_gl_cube_map_target(z), level, x, y, x_buffer, y_buffer, width, height); break;
	}
	bool result = !check_gl_error("gl_context::texture_replace_from_buffer", &tb);
	texture_unbind(tb.tt, tmp_id);

	if (result && gen_mipmap)
		result = texture_generate_mipmaps(tb, tb.tt == TT_CUBEMAP ? 2 : (int)tb.tt);

	return result;
}

bool gl_context::texture_generate_mipmaps(texture_base& tb, unsigned int dim) const
{
	GLuint tmp_id = texture_bind(tb.tt,get_gl_id(tb.handle));

	std::string error_string;
	bool result = generate_mipmaps(dim, &error_string);
	if (result)
		tb.have_mipmaps = true;
	else
		error(std::string("gl_context::texture_generate_mipmaps: ") + error_string);

	texture_unbind(tb.tt, tmp_id);
	return result;
}

bool gl_context::texture_destruct(texture_base& tb) const
{
	if (!is_created()) {
		error("gl_context::texture_destruct: attempt to destruct not created texture", &tb);
		return false;
	}
	GLuint tex_id = get_gl_id(tb.handle);
	glDeleteTextures(1, &tex_id);
	bool result = !check_gl_error("gl_context::texture_destruct", &tb);
	tb.handle = 0;
	return result;
}

bool gl_context::texture_set_state(const texture_base& tb) const
{
	if (tb.tt == TT_UNDEF) {
		error("gl_context::texture_set_state: attempt to set state on texture without type", &tb);
		return false;
	}
	GLuint tex_id = (GLuint&) tb.handle - 1;
	if (tex_id == -1) {
		error("gl_context::texture_set_state: attempt of setting texture state of not created texture", &tb);
		return false;
	}
	GLint tmp_id = texture_bind(tb.tt, tex_id);

	glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_MIN_FILTER, map_to_gl(tb.min_filter));
	glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_MAG_FILTER, map_to_gl(tb.mag_filter));
	glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_COMPARE_FUNC, map_to_gl(tb.compare_function));
	glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_COMPARE_MODE, (tb.use_compare_function ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE));
	glTexParameterf(get_tex_dim(tb.tt), GL_TEXTURE_PRIORITY, tb.priority);
	if (tb.min_filter == TF_ANISOTROP)
		glTexParameterf(get_tex_dim(tb.tt), GL_TEXTURE_MAX_ANISOTROPY_EXT, tb.anisotropy);
	else
		glTexParameterf(get_tex_dim(tb.tt), GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
	if (tb.border_color[0] >= 0.0f)
		glTexParameterfv(get_tex_dim(tb.tt), GL_TEXTURE_BORDER_COLOR, tb.border_color);
	glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_WRAP_S, map_to_gl(tb.wrap_s));
	if (tb.tt > TT_1D)
		glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_WRAP_T, map_to_gl(tb.wrap_t));
	if (tb.tt == TT_3D)
		glTexParameteri(get_tex_dim(tb.tt), GL_TEXTURE_WRAP_R, map_to_gl(tb.wrap_r));

	bool result = !check_gl_error("gl_context::texture_set_state", &tb);
	texture_unbind(tb.tt, tmp_id);
	return result;
}

bool gl_context::texture_enable(
						texture_base& tb, 
						int tex_unit, unsigned int dim) const
{
	if (dim < 1 || dim > 3) {
		error("gl_context::texture_enable: invalid texture dimension", &tb);
		return false;
	}
	GLuint tex_id = (GLuint&) tb.handle - 1;
	if (tex_id == -1) {
		error("gl_context::texture_enable: texture not created", &tb);
		return false;
	}
	if (tex_unit >= 0) {
		if (!GLEW_VERSION_1_3) {
			error("gl_context::texture_enable: multi texturing not supported", &tb);
			return false;
		}
		glActiveTexture(GL_TEXTURE0+tex_unit);
	}
	GLint& old_binding = (GLint&) tb.user_data;
	glGetIntegerv(get_tex_bind(tb.tt), &old_binding);
	++old_binding;
	glBindTexture(get_tex_dim(tb.tt), tex_id);
	glEnable(get_tex_dim(tb.tt));
	bool result = !check_gl_error("gl_context::texture_enable", &tb);
	if (tex_unit >= 0)
		glActiveTexture(GL_TEXTURE0);
	return result;
}

bool gl_context::texture_disable(
						texture_base& tb, 
						int tex_unit, unsigned int dim) const
{
	if (dim < 1 || dim > 3) {
		error("gl_context::texture_disable: invalid texture dimension", &tb);
		return false;
	}
	if (tex_unit == -2) {
		error("gl_context::texture_disable: invalid texture unit", &tb);
		return false;
	}
	GLuint old_binding = (const GLuint&) tb.user_data;
	--old_binding;
	if (tex_unit >= 0)
		glActiveTexture(GL_TEXTURE0+tex_unit);
	glDisable(get_tex_dim(tb.tt));
	bool result = !check_gl_error("gl_context::texture_disable", &tb);
	glBindTexture(get_tex_dim(tb.tt), old_binding);
	if (tex_unit >= 0)
		glActiveTexture(GL_TEXTURE0);
	return result;
}

bool gl_context::render_buffer_create(
	render_component& rc,
	cgv::data::component_format& cf,
	int& _width, int& _height) const
{
	if (!GLEW_VERSION_3_0) {
		error("gl_context::render_buffer_create: frame buffer objects not supported", &rc);
		return false;
	}
	if (_width == -1)
		_width = get_width();
	if (_height == -1)
		_height = get_height();

	GLuint rb_id;
	glGenRenderbuffers(1, &rb_id);
	glBindRenderbuffer(GL_RENDERBUFFER, rb_id);

	GLuint& gl_format = (GLuint&)rc.internal_format;
	unsigned i = find_best_match(cf, color_buffer_formats);
	cgv::data::component_format best_cf(color_buffer_formats[i]);
	gl_format = gl_color_buffer_format_ids[i];

	i = find_best_match(cf, depth_formats, &best_cf);
	if (i != -1) {
		best_cf = cgv::data::component_format(depth_formats[i]);
		gl_format = gl_depth_format_ids[i];
	}

	cf = best_cf;

	glRenderbufferStorage(GL_RENDERBUFFER, gl_format, _width, _height);

	if (check_gl_error("gl_context::render_buffer_create", &rc))
		return false;
	rc.handle = get_handle(rb_id);
	return true;
}

bool gl_context::render_buffer_destruct(render_component& rc) const
{
	if (!GLEW_VERSION_3_0) {
		error("gl_context::render_buffer_destruct: frame buffer objects not supported", &rc);
		return false;
	}
	GLuint rb_id = ((GLuint&) rc.handle)+1;
	glDeleteRenderbuffers(1, &rb_id);
	if (check_gl_error("gl_context::render_buffer_destruct", &rc))
		return false;
	rc.handle = 0;
	return true;
}

bool gl_context::frame_buffer_create(frame_buffer_base& fbb) const
{
	if (!check_fbo_support("gl_context::frame_buffer_create", &fbb))
		return false;
	
	if (!context::frame_buffer_create(fbb))
		return false;

	// allocate framebuffer object
	GLuint fbo_id = 0;
	glGenFramebuffers(1, &fbo_id);
	if (fbo_id == 0) {
		error("gl_context::frame_buffer_create: could not allocate framebuffer object", &fbb);
		return false;
	}
	fbb.handle = get_handle(fbo_id);
	return true;
}

bool gl_context::frame_buffer_enable(frame_buffer_base& fbb)
{
	if (!context::frame_buffer_enable(fbb))
		return false;
	std::vector<int> buffers;
	get_buffer_list(fbb, buffers, GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_FRAMEBUFFER, get_gl_id(fbb.handle));

	if (buffers.size() == 1)
		glDrawBuffer(buffers[0]);
	else if (buffers.size() > 1) {
		glDrawBuffers(GLsizei(buffers.size()), reinterpret_cast<GLenum*>(&buffers[0]));
	}
	else {
		error("gl_context::frame_buffer_enable: no attached draw buffer selected!!", &fbb);
		return false;
	}
	return true;
}

/// disable the framebuffer object
bool gl_context::frame_buffer_disable(frame_buffer_base& fbb)
{
	if (!context::frame_buffer_disable(fbb))
		return false;
	if (frame_buffer_stack.empty())
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	else
		glBindFramebuffer(GL_FRAMEBUFFER, get_gl_id(frame_buffer_stack.top()->handle));
	return true;
}

bool gl_context::frame_buffer_destruct(frame_buffer_base& fbb)
{
	if (!context::frame_buffer_destruct(fbb))
		return false;
	GLuint fbo_id = get_gl_id(fbb.handle);
	glDeleteFramebuffers(1, &fbo_id);
	fbb.handle = 0;
	return true;
}

bool gl_context::frame_buffer_attach(frame_buffer_base& fbb, const render_component& rb, bool is_depth, int i) const
{
	if (!context::frame_buffer_attach(fbb, rb, is_depth, i))
		return false;
	GLint old_binding;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_binding);
	glBindFramebuffer(GL_FRAMEBUFFER, get_gl_id(fbb.handle));
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, 
			is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0 + i,
			GL_RENDERBUFFER, 
			get_gl_id(rb.handle));
	glBindFramebuffer(GL_FRAMEBUFFER, old_binding);
	return true;
}

/// attach 2d texture to depth buffer if it is a depth texture, to stencil if it is a stencil texture or to the i-th color attachment if it is a color texture
bool gl_context::frame_buffer_attach(frame_buffer_base& fbb, 
												 const texture_base& t, bool is_depth,
												 int level, int i, int z_or_cube_side) const
{
	if (!context::frame_buffer_attach(fbb, t, is_depth, level, i, z_or_cube_side))
		return false;
	GLint old_binding;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_binding);
	glBindFramebuffer(GL_FRAMEBUFFER, get_gl_id(fbb.handle));

	if (z_or_cube_side == -1) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, 
			is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0+i,
			GL_TEXTURE_2D, get_gl_id(t.handle), level);
	}
	else {
		if (t.tt == TT_CUBEMAP) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, 
				GL_COLOR_ATTACHMENT0+i, 
				get_gl_cube_map_target(z_or_cube_side), get_gl_id(t.handle), level);
		}
		else {
			glFramebufferTexture3D(GL_FRAMEBUFFER, 
				GL_COLOR_ATTACHMENT0+i, 
				GL_TEXTURE_3D, get_gl_id(t.handle), level, z_or_cube_side);
		}
	}
	bool result = !check_gl_error("gl_context::frame_buffer_attach", &fbb);
	glBindFramebuffer(GL_FRAMEBUFFER, old_binding);
	return result;
}

/// check for completeness, if not complete, get the reason in last_error
bool gl_context::frame_buffer_is_complete(const frame_buffer_base& fbb) const
{
	if (fbb.handle == 0) {
		error("gl_context::frame_buffer_is_complete: attempt to check completeness on frame buffer that is not created", &fbb);
		return false;
	}
	GLint old_binding;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_binding);
	glBindFramebuffer(GL_FRAMEBUFFER, get_gl_id(fbb.handle));
	GLenum error = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, old_binding);
	switch (error) {
	case GL_FRAMEBUFFER_COMPLETE:
		return true;
	case GL_FRAMEBUFFER_UNDEFINED:
		fbb.last_error = "undefined framebuffer";
		return false;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		fbb.last_error = "incomplete attachment";
		return false;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      fbb.last_error = "incomplete or missing attachment";
		return false;
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
      fbb.last_error = "incomplete multisample";
		return false;
    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
      fbb.last_error = "incomplete layer targets";
		return false;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
      fbb.last_error = "incomplete draw buffer";
		return false;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
      fbb.last_error = "incomplete read buffer";
		return false;
    case GL_FRAMEBUFFER_UNSUPPORTED:
      fbb.last_error = "framebuffer objects unsupported";
		return false;
	}
	fbb.last_error = "unknown error";
	return false;
}

int gl_context::frame_buffer_get_max_nr_color_attachments() const
{
	if (!check_fbo_support("gl_context::frame_buffer_get_max_nr_color_attachments"))
		return 0;

	GLint nr;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &nr);
	return nr;
}

int gl_context::frame_buffer_get_max_nr_draw_buffers() const
{
	if (!check_fbo_support("gl_context::frame_buffer_get_max_nr_draw_buffers"))
		return 0;

	GLint nr;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &nr);
	return nr;
}

GLuint gl_shader_type[] = 
{
	0, GL_COMPUTE_SHADER, GL_VERTEX_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER
};

void gl_context::shader_code_destruct(render_component& sc) const
{
	if (sc.handle == 0) {
		error("gl_context::shader_code_destruct: shader not created", &sc);
		return;
	}
	glDeleteShader(get_gl_id(sc.handle));
	check_gl_error("gl_context::shader_code_destruct", &sc);
}

bool gl_context::shader_code_create(render_component& sc, ShaderType st, const std::string& source) const
{
	if (!check_shader_support(st, "gl_context::shader_code_create", &sc))
		return false;

	GLuint s_id = glCreateShader(gl_shader_type[st]);
	if (s_id == -1) {
		error(std::string("gl_context::shader_code_create: ")+gl_error(), &sc);
		return false;
	}
	sc.handle = get_handle(s_id);

	const char* s = source.c_str();
	glShaderSource(s_id, 1, &s,NULL);
	if (check_gl_error("gl_context::shader_code_create", &sc))
		return false;

	return true;
}

bool gl_context::shader_code_compile(render_component& sc) const
{
	if (sc.handle == 0) {
		error("gl_context::shader_code_compile: shader not created", &sc);
		return false;
	}
	GLuint s_id = get_gl_id(sc.handle);
	glCompileShader(s_id);
	int result;
	glGetShaderiv(s_id, GL_COMPILE_STATUS, &result); 
	if (result == 1)
		return true;
	sc.last_error = std::string();
	GLint infologLength = 0;
	glGetShaderiv(s_id, GL_INFO_LOG_LENGTH, &infologLength);
	if (infologLength > 0) {
		int charsWritten = 0;
		char *infoLog = (char *)malloc(infologLength);
		glGetShaderInfoLog(s_id, infologLength, &charsWritten, infoLog);
		sc.last_error = infoLog;
		free(infoLog);
	}
	return false;
}

bool gl_context::shader_program_create(shader_program_base& spb) const
{
	if (!check_shader_support(ST_VERTEX, "gl_context::shader_program_create", &spb))
		return false;
	spb.handle = get_handle(glCreateProgram());
	return true;
}

void gl_context::shader_program_attach(shader_program_base& spb, const render_component& sc) const
{
	if (spb.handle == 0) {
		error("gl_context::shader_program_attach: shader program not created", &spb);
		return;
	}
	glAttachShader(get_gl_id(spb.handle), get_gl_id(sc.handle));
}

void gl_context::shader_program_detach(shader_program_base& spb, const render_component& sc) const
{
	if (spb.handle == 0) {
		error("gl_context::shader_program_detach: shader program not created", &spb);
		return;
	}
	glDetachShader(get_gl_id(spb.handle), get_gl_id(sc.handle));
}

bool gl_context::shader_program_link(shader_program_base& spb) const
{
	if (spb.handle == 0) {
		error("gl_context::shader_program_link: shader program not created", &spb);
		return false;
	}
	GLuint p_id = get_gl_id(spb.handle);
	glLinkProgram(p_id); 
	int result;
	glGetProgramiv(p_id, GL_LINK_STATUS, &result); 
	if (result == 1)
		return true;
	GLint infologLength = 0;
	glGetProgramiv(p_id, GL_INFO_LOG_LENGTH, &infologLength);
	if (infologLength > 0) {
		GLsizei charsWritten = 0;
		char *infoLog = (char *)malloc(infologLength);
		glGetProgramInfoLog(p_id, infologLength, &charsWritten, infoLog);
		spb.last_error = infoLog;
		error(std::string("gl_context::shader_program_link\n")+infoLog, &spb);
		free(infoLog);
	}
	return false;
}

bool gl_context::shader_program_set_state(shader_program_base& spb) const
{
	if (spb.handle == 0) {
		error("gl_context::shader_program_set_state: shader program not created", &spb);
		return false;
	}
	GLuint p_id = get_gl_id(spb.handle);
	glProgramParameteriARB(p_id, GL_GEOMETRY_VERTICES_OUT_ARB, spb.geometry_shader_output_count);
	glProgramParameteriARB(p_id, GL_GEOMETRY_INPUT_TYPE_ARB, map_to_gl(spb.geometry_shader_input_type));
	glProgramParameteriARB(p_id, GL_GEOMETRY_OUTPUT_TYPE_ARB, map_to_gl(spb.geometry_shader_output_type));
	return true;
}

bool gl_context::shader_program_enable(shader_program_base& spb)
{
	if (!context::shader_program_enable(spb))
		return false;
	glUseProgram(get_gl_id(spb.handle));
	
	shader_program& prog = static_cast<shader_program&>(spb);
	if (auto_set_lights_in_current_shader_program && spb.does_use_lights())
		set_current_lights(prog);
	if (auto_set_material_in_current_shader_program && spb.does_use_material())
		set_current_material(prog);
	if (auto_set_view_in_current_shader_program && spb.does_use_view())
		set_current_view(prog);
	if (auto_set_gamma_in_current_shader_program && spb.does_use_gamma())
		prog.set_uniform(*this, "gamma", gamma);
	return true;
}

bool gl_context::shader_program_disable(shader_program_base& spb)
{
	if (!context::shader_program_disable(spb)) 
		return false;
	if (shader_program_stack.empty())
		glUseProgram(0);
	else
		glUseProgram(get_gl_id(shader_program_stack.top()->handle));
	return true;
}

bool gl_context::shader_program_destruct(shader_program_base& spb)
{
	if (!context::shader_program_destruct(spb))
		return false;
	glDeleteProgram(get_gl_id(spb.handle));
	return true;
}

int  gl_context::get_uniform_location(const shader_program_base& spb, const std::string& name) const
{
	return glGetUniformLocation(get_gl_id(spb.handle), name.c_str());
}

std::string value_type_index_to_string(type_descriptor td)
{
	std::string res = cgv::type::info::get_type_name(td.coordinate_type);
	switch (td.element_type) {
	case ET_VECTOR:
		res = std::string("vector<") + res + "," + cgv::utils::to_string(td.nr_rows) + ">";
		break;
	case ET_MATRIX:
		res = std::string("matrix<") + res + "," + cgv::utils::to_string(td.nr_rows) + "," + cgv::utils::to_string(td.nr_columns) + ">";
		if (td.is_row_major)
			res += "^T";
		break;
	}
	if (td.is_array)
		res += "[]";
	return res;
}

bool gl_context::set_uniform_void(shader_program_base& spb, int loc, type_descriptor value_type, const void* value_ptr) const
{
	if (value_type.is_array) {
		error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") array type not supported, please use set_uniform_array instead.", &spb);
		return false;
	}
	if (!spb.handle) {
		error("gl_context::set_uniform_void() called on not created program", &spb);
		return false;
	}
	bool not_current = shader_program_stack.empty() || shader_program_stack.top() != &spb;
	if (not_current)
		glUseProgram(get_gl_id(spb.handle));
	bool res = true;
	switch (value_type.element_type) {
	case ET_VALUE:
		switch (value_type.coordinate_type) {
		case TI_BOOL: glUniform1i(loc, *reinterpret_cast<const bool*>(value_ptr) ? 1 : 0); break;
		case TI_UINT8: glUniform1ui(loc, *reinterpret_cast<const uint8_type*>(value_ptr)); break;
		case TI_UINT16: glUniform1ui(loc, *reinterpret_cast<const uint16_type*>(value_ptr)); break;
		case TI_UINT32: glUniform1ui(loc, *reinterpret_cast<const uint32_type*>(value_ptr)); break;
		case TI_INT8: glUniform1i(loc, *reinterpret_cast<const int8_type*>(value_ptr)); break;
		case TI_INT16: glUniform1i(loc, *reinterpret_cast<const int16_type*>(value_ptr)); break;
		case TI_INT32: glUniform1i(loc, *reinterpret_cast<const int32_type*>(value_ptr)); break;
		case TI_FLT32: glUniform1f(loc, *reinterpret_cast<const flt32_type*>(value_ptr)); break;
		case TI_FLT64: glUniform1d(loc, *reinterpret_cast<const flt64_type*>(value_ptr)); break;
		default:
			error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
			res = false; break;
		}
		break;
	case ET_VECTOR:
		switch (value_type.nr_rows) {
		case 2:
			switch (value_type.coordinate_type) {
			case TI_BOOL:   glUniform2i(loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0); break;
			case TI_UINT8:  glUniform2ui(loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1]); break;
			case TI_UINT16: glUniform2ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1]); break;
			case TI_UINT32: glUniform2ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1]); break;
			case TI_INT8:   glUniform2i(loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1]); break;
			case TI_INT16:  glUniform2i(loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1]); break;
			case TI_INT32:  glUniform2i(loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1]); break;
			case TI_FLT32:  glUniform2f(loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1]); break;
			case TI_FLT64:  glUniform2d(loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1]); break;
			default:
				error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false; break;
			}
			break;
		case 3:
			switch (value_type.coordinate_type) {
			case TI_BOOL:   glUniform3i(loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[2] ? 1 : 0); break;
			case TI_UINT8:  glUniform3ui(loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1], reinterpret_cast<const uint8_type*> (value_ptr)[2]); break;
			case TI_UINT16: glUniform3ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1], reinterpret_cast<const uint16_type*>(value_ptr)[2]); break;
			case TI_UINT32: glUniform3ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1], reinterpret_cast<const uint32_type*>(value_ptr)[2]); break;
			case TI_INT8:   glUniform3i(loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1], reinterpret_cast<const int8_type*>  (value_ptr)[2]); break;
			case TI_INT16:  glUniform3i(loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1], reinterpret_cast<const int16_type*> (value_ptr)[2]); break;
			case TI_INT32:  glUniform3i(loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1], reinterpret_cast<const int32_type*> (value_ptr)[2]); break;
			case TI_FLT32:  glUniform3f(loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1], reinterpret_cast<const flt32_type*> (value_ptr)[2]); break;
			case TI_FLT64:  glUniform3d(loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1], reinterpret_cast<const flt64_type*> (value_ptr)[2]); break;
			default:
				error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false; break;
			}
			break;
		case 4:
			switch (value_type.coordinate_type) {
			case TI_BOOL:   glUniform4i(loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[2] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[3] ? 1 : 0); break;
			case TI_UINT8:  glUniform4ui(loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1], reinterpret_cast<const uint8_type*> (value_ptr)[2], reinterpret_cast<const uint8_type*> (value_ptr)[3]); break;
			case TI_UINT16: glUniform4ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1], reinterpret_cast<const uint16_type*>(value_ptr)[2], reinterpret_cast<const uint16_type*>(value_ptr)[3]); break;
			case TI_UINT32: glUniform4ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1], reinterpret_cast<const uint32_type*>(value_ptr)[2], reinterpret_cast<const uint32_type*>(value_ptr)[3]); break;
			case TI_INT8:   glUniform4i(loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1], reinterpret_cast<const int8_type*>  (value_ptr)[2], reinterpret_cast<const int8_type*>  (value_ptr)[3]); break;
			case TI_INT16:  glUniform4i(loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1], reinterpret_cast<const int16_type*> (value_ptr)[2], reinterpret_cast<const int16_type*> (value_ptr)[3]); break;
			case TI_INT32:  glUniform4i(loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1], reinterpret_cast<const int32_type*> (value_ptr)[2], reinterpret_cast<const int32_type*> (value_ptr)[3]); break;
			case TI_FLT32:  glUniform4f(loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1], reinterpret_cast<const flt32_type*> (value_ptr)[2], reinterpret_cast<const flt32_type*> (value_ptr)[3]); break;
			case TI_FLT64:  glUniform4d(loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1], reinterpret_cast<const flt64_type*> (value_ptr)[2], reinterpret_cast<const flt64_type*> (value_ptr)[3]); break;
			default:
				error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false; break;
			}
			break;
		}
		break;
	case ET_MATRIX:
		switch (value_type.coordinate_type) {
		case TI_FLT32:
			switch (value_type.nr_rows) {
			case 2:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix2fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 3: glUniformMatrix2x3fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 4: glUniformMatrix2x4fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			case 3:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix3x2fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 3: glUniformMatrix3fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 4: glUniformMatrix3x4fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			case 4:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix4x2fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 3: glUniformMatrix4x3fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				case 4: glUniformMatrix4fv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt32_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			default:
				error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of rows outside [2,..,4].", &spb);
				res = false; break;
			}
			break;
		case TI_FLT64:
			switch (value_type.nr_rows) {
			case 2:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix2dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 3: glUniformMatrix2x3dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 4: glUniformMatrix2x4dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			case 3:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix3x2dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 3: glUniformMatrix3dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 4: glUniformMatrix3x4dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			case 4:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix4x2dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 3: glUniformMatrix4x3dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				case 4: glUniformMatrix4dv(loc, 1, !value_type.is_row_major, reinterpret_cast<const flt64_type*> (value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..,4].", &spb);
					res = false; break;
				}
				break;
			default:
				error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") matrix number of rows outside [2,..,4].", &spb);
				res = false; break;
			}
			break;
		default:
			error(std::string("gl_context::set_uniform_void(") + value_type_index_to_string(value_type) + ") non float coordinate type not supported.", &spb);
			res = false; break;
		}
		break;
	}
	if (not_current)
		glUseProgram(shader_program_stack.empty() ? 0 : get_gl_id(shader_program_stack.top()->handle));

	if (check_gl_error("gl_context::set_uniform_void()", &spb))
		res = false;
	return res;
}

bool gl_context::set_uniform_array_void(shader_program_base& spb, int loc, type_descriptor value_type, const void* value_ptr, size_t nr_elements) const
{
	if (!value_type.is_array) {
		error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") non array type not allowed.", &spb);
		return false;
	}
	if (!spb.handle) {
		error("gl_context::set_uniform_array_void() called on not created program", &spb);
		return false;
	}
	bool not_current = shader_program_stack.empty() || shader_program_stack.top() != &spb;
	if (not_current)
		glUseProgram(get_gl_id(spb.handle));
	bool res = true;
	switch (value_type.coordinate_type) {
	case TI_INT32:
		switch (value_type.element_type) {
		case ET_VALUE:
			glUniform1iv(loc, GLsizei(nr_elements), reinterpret_cast<const int32_type*>(value_ptr));
			break;
		case ET_VECTOR:
			switch (value_type.nr_rows) {
			case 2: glUniform2iv(loc, GLsizei(nr_elements), reinterpret_cast<const int32_type*>(value_ptr)); break;
			case 3: glUniform3iv(loc, GLsizei(nr_elements), reinterpret_cast<const int32_type*>(value_ptr)); break;
			case 4: glUniform4iv(loc, GLsizei(nr_elements), reinterpret_cast<const int32_type*>(value_ptr)); break;
			default:
				error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") vector dimension outside [2,..4].", &spb);
				res = false;
				break;
			}
			break;
		case ET_MATRIX:
			error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") type not supported.", &spb);
			res = false;
			break;
		}
		break;
	case TI_UINT32:
		switch (value_type.element_type) {
		case ET_VALUE:
			glUniform1uiv(loc, GLsizei(nr_elements), reinterpret_cast<const uint32_type*>(value_ptr));
			break;
		case ET_VECTOR:
			switch (value_type.nr_rows) {
			case 2: glUniform2uiv(loc, GLsizei(nr_elements), reinterpret_cast<const uint32_type*>(value_ptr)); break;
			case 3:	glUniform3uiv(loc, GLsizei(nr_elements), reinterpret_cast<const uint32_type*>(value_ptr)); break;
			case 4:	glUniform4uiv(loc, GLsizei(nr_elements), reinterpret_cast<const uint32_type*>(value_ptr)); break;
			default:
				error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") vector dimension outside [2,..4].", &spb);
				res = false;
				break;
			}
			break;
		case ET_MATRIX:
			error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") type not supported.", &spb);
			res = false;
			break;
		}
		break;
	case TI_FLT32:
		switch (value_type.element_type) {
		case ET_VALUE:
			glUniform1fv(loc, GLsizei(nr_elements), reinterpret_cast<const flt32_type*>(value_ptr));
			break;
		case ET_VECTOR:
			switch (value_type.nr_rows) {
			case 2: glUniform2fv(loc, GLsizei(nr_elements), reinterpret_cast<const flt32_type*>(value_ptr)); break;
			case 3:	glUniform3fv(loc, GLsizei(nr_elements), reinterpret_cast<const flt32_type*>(value_ptr)); break;
			case 4:	glUniform4fv(loc, GLsizei(nr_elements), reinterpret_cast<const flt32_type*>(value_ptr)); break;
			default:
				error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") vector dimension outside [2,..4].", &spb);
				res = false;
				break;
			}
			break;
		case ET_MATRIX:
			switch (value_type.nr_rows) {
			case 2:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix2fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				case 3: glUniformMatrix2x3fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				case 4: glUniformMatrix2x4fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..4].", &spb);
					res = false;
					break;
				}
				break;
			case 3:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix3x2fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				case 3:	glUniformMatrix3fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr));	break;
				case 4:	glUniformMatrix3x4fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				default:
					error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..4].", &spb);
					res = false;
					break;
				}
				break;
			case 4:
				switch (value_type.nr_columns) {
				case 2: glUniformMatrix4x2fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				case 3:	glUniformMatrix4x3fv(loc, GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr)); break;
				case 4:	glUniformMatrix4fv(loc,   GLsizei(nr_elements), value_type.is_row_major, reinterpret_cast<const flt32_type*>(value_ptr));	break;
				default:
					error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") matrix number of columns outside [2,..4].", &spb);
					res = false;
					break;
				}
				break;
			default:
				error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") matrix number of rows outside [2,..4].", &spb);
				res = false;
				break;
			}
			break;
		}
		break;
	default:
		error(std::string("gl_context::set_uniform_array_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type (only int32, uint32, and flt32 supported).", &spb);
		res = false;
		break;
	}
	if (check_gl_error("gl_context::set_uniform_array_void()", &spb))
		res = false;

	if (not_current)
		glUseProgram(shader_program_stack.empty() ? 0 : get_gl_id(shader_program_stack.top()->handle));

	return res;
}

int  gl_context::get_attribute_location(const shader_program_base& spb, const std::string& name) const
{
	return glGetAttribLocation(get_gl_id(spb.handle), name.c_str());
}

bool gl_context::set_attribute_void(shader_program_base& spb, int loc, type_descriptor value_type, const void* value_ptr) const
{
	if (!spb.handle) {
		error("gl_context::set_attribute_void() called on not created program", &spb);
		return false;
	}
	bool not_current = shader_program_stack.empty() || shader_program_stack.top() != &spb;
	if (not_current)
		glUseProgram(get_gl_id(spb.handle));
	bool res = true;
	switch (value_type.element_type) {
	case ET_VALUE:
		switch (value_type.coordinate_type) {
		case TI_BOOL:   glVertexAttrib1s(loc, *reinterpret_cast<const bool*>(value_ptr) ? 1 : 0); break;
		case TI_INT8:   glVertexAttrib1s(loc, *reinterpret_cast<const int8_type*>(value_ptr)); break;
		case TI_INT16:  glVertexAttrib1s(loc, *reinterpret_cast<const int16_type*>(value_ptr)); break;
		case TI_INT32:  glVertexAttribI1i(loc, *reinterpret_cast<const int32_type*>(value_ptr)); break;
		case TI_UINT8:  glVertexAttrib1s(loc, *reinterpret_cast<const uint8_type*>(value_ptr)); break;
		case TI_UINT16: glVertexAttribI1ui(loc, *reinterpret_cast<const uint16_type*>(value_ptr)); break;
		case TI_UINT32: glVertexAttribI1ui(loc, *reinterpret_cast<const uint32_type*>(value_ptr)); break;
		case TI_FLT32:  glVertexAttrib1f(loc, *reinterpret_cast<const flt32_type*>(value_ptr)); break;
		case TI_FLT64:  glVertexAttrib1d(loc, *reinterpret_cast<const flt64_type*>(value_ptr)); break;
		default:
			error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") type not supported!", &spb);
			res = false;
			break;
		}
		break;
	case ET_VECTOR:
		switch (value_type.nr_rows) {
		case 2:
			switch (value_type.coordinate_type) {
			case TI_BOOL:    glVertexAttrib2s(loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0); break;
			case TI_UINT8:   glVertexAttrib2s(loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1]); break;
			case TI_UINT16: glVertexAttribI2ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1]); break;
			case TI_UINT32: glVertexAttribI2ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1]); break;
			case TI_INT8:    glVertexAttrib2s(loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1]); break;
			case TI_INT16:   glVertexAttrib2s(loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1]); break;
			case TI_INT32:  glVertexAttribI2i(loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1]); break;
			case TI_FLT32:   glVertexAttrib2f(loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1]); break;
			case TI_FLT64:   glVertexAttrib2d(loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1]); break;
			default:
				error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false;
				break;
			}
			break;
		case 3:
			switch (value_type.coordinate_type) {
			case TI_BOOL:    glVertexAttrib3s (loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[2] ? 1 : 0); break;
			case TI_UINT8:   glVertexAttrib3s (loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1], reinterpret_cast<const uint8_type*> (value_ptr)[2]); break;
			case TI_UINT16: glVertexAttribI3ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1], reinterpret_cast<const uint16_type*>(value_ptr)[2]); break;
			case TI_UINT32: glVertexAttribI3ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1], reinterpret_cast<const uint32_type*>(value_ptr)[2]); break;
			case TI_INT8:    glVertexAttrib3s (loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1], reinterpret_cast<const int8_type*>  (value_ptr)[2]); break;
			case TI_INT16:   glVertexAttrib3s (loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1], reinterpret_cast<const int16_type*> (value_ptr)[2]); break;
			case TI_INT32:  glVertexAttribI3i (loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1], reinterpret_cast<const int32_type*> (value_ptr)[2]); break;
			case TI_FLT32:   glVertexAttrib3f (loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1], reinterpret_cast<const flt32_type*> (value_ptr)[2]); break;
			case TI_FLT64:   glVertexAttrib3d (loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1], reinterpret_cast<const flt64_type*> (value_ptr)[2]); break;
			default:
				error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false; break;
			}
			break;
		case 4:
			switch (value_type.coordinate_type) {
			case TI_BOOL:    glVertexAttrib4s (loc, reinterpret_cast<const bool*>(value_ptr)[0] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[1] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[2] ? 1 : 0, reinterpret_cast<const bool*>(value_ptr)[3] ? 1 : 0); break;
			case TI_UINT8:   glVertexAttrib4s (loc, reinterpret_cast<const uint8_type*> (value_ptr)[0], reinterpret_cast<const uint8_type*> (value_ptr)[1], reinterpret_cast<const uint8_type*> (value_ptr)[2], reinterpret_cast<const uint8_type*> (value_ptr)[3]); break;
			case TI_UINT16: glVertexAttribI4ui(loc, reinterpret_cast<const uint16_type*>(value_ptr)[0], reinterpret_cast<const uint16_type*>(value_ptr)[1], reinterpret_cast<const uint16_type*>(value_ptr)[2], reinterpret_cast<const uint16_type*>(value_ptr)[3]); break;
			case TI_UINT32: glVertexAttribI4ui(loc, reinterpret_cast<const uint32_type*>(value_ptr)[0], reinterpret_cast<const uint32_type*>(value_ptr)[1], reinterpret_cast<const uint32_type*>(value_ptr)[2], reinterpret_cast<const uint32_type*>(value_ptr)[3]); break;
			case TI_INT8:    glVertexAttrib4s (loc, reinterpret_cast<const int8_type*>  (value_ptr)[0], reinterpret_cast<const int8_type*>  (value_ptr)[1], reinterpret_cast<const int8_type*>  (value_ptr)[2], reinterpret_cast<const int8_type*>  (value_ptr)[3]); break;
			case TI_INT16:   glVertexAttrib4s (loc, reinterpret_cast<const int16_type*> (value_ptr)[0], reinterpret_cast<const int16_type*> (value_ptr)[1], reinterpret_cast<const int16_type*> (value_ptr)[2], reinterpret_cast<const int16_type*> (value_ptr)[3]); break;
			case TI_INT32:  glVertexAttribI4i (loc, reinterpret_cast<const int32_type*> (value_ptr)[0], reinterpret_cast<const int32_type*> (value_ptr)[1], reinterpret_cast<const int32_type*> (value_ptr)[2], reinterpret_cast<const int32_type*> (value_ptr)[3]); break;
			case TI_FLT32:   glVertexAttrib4f (loc, reinterpret_cast<const flt32_type*> (value_ptr)[0], reinterpret_cast<const flt32_type*> (value_ptr)[1], reinterpret_cast<const flt32_type*> (value_ptr)[2], reinterpret_cast<const flt32_type*> (value_ptr)[3]); break;
			case TI_FLT64:   glVertexAttrib4d (loc, reinterpret_cast<const flt64_type*> (value_ptr)[0], reinterpret_cast<const flt64_type*> (value_ptr)[1], reinterpret_cast<const flt64_type*> (value_ptr)[2], reinterpret_cast<const flt64_type*> (value_ptr)[3]); break;
			default:
				error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") unsupported coordinate type.", &spb);
				res = false;
				break;
			}
			break;
		default:
			error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") vector dimension outside [2..4]", &spb);
			res = false;
			break;
		}
		break;
	case ET_MATRIX:
		error(std::string("gl_context::set_attribute_void(") + value_type_index_to_string(value_type) + ") matrix type not supported!", &spb);
		res = false;
		break;
	}
	if (not_current)
		glUseProgram(shader_program_stack.empty() ? 0 : get_gl_id(shader_program_stack.top()->handle));

	if (check_gl_error("gl_context::set_uniform_array_void()", &spb))
		res = false;
	return res;
}

bool gl_context::attribute_array_binding_create(attribute_array_binding_base& aab) const
{
	if (!GLEW_VERSION_3_0) {
		error("gl_context::attribute_array_binding_create() array attribute bindings not supported", &aab);
		return false;
	}
	GLuint a_id;
	glGenVertexArrays(1, &a_id);
	if (a_id == -1) {
		error(std::string("gl_context::attribute_array_binding_create(): ") + gl_error(), &aab);
		return false;
	}
	aab.handle = get_handle(a_id);
	return true;
}

bool gl_context::attribute_array_binding_destruct(attribute_array_binding_base& aab)
{
	if (&aab == attribute_array_binding_stack.top())
		glBindVertexArray(0);
	if (!context::attribute_array_binding_destruct(aab))
		return false;
	if (!aab.handle) {
		error("gl_context::attribute_array_binding_destruct(): called on not created attribute array binding", &aab);
		return false;
	}
	GLuint a_id = get_gl_id(aab.handle);
	glDeleteVertexArrays(1, &a_id);
	return !check_gl_error("gl_context::attribute_array_binding_destruct");
}

bool gl_context::attribute_array_binding_enable(attribute_array_binding_base& aab)
{
	if (!context::attribute_array_binding_enable(aab))
		return false;
	glBindVertexArray(get_gl_id(aab.handle));
	return !check_gl_error("gl_context::attribute_array_binding_enable");
}

bool gl_context::attribute_array_binding_disable(attribute_array_binding_base& aab)
{
	if (!context::attribute_array_binding_disable(aab))
		return false;
	if (attribute_array_binding_stack.empty())
		glBindVertexArray(0);
	else
		glBindVertexArray(get_gl_id(attribute_array_binding_stack.top()->handle));
	return true;
}

bool gl_context::set_element_array(attribute_array_binding_base* aab, const vertex_buffer_base* vbb) const
{
	if (!vbb) {
		error("gl_context::set_element_array(): called without a vertex buffer object", aab);
		return false;
	}
	if (!vbb->handle) {
		error("gl_context::set_element_array(): called with not created vertex buffer object", vbb);
		return false;
	}
	if (vbb->type != VBT_INDICES) {
		error("gl_context::set_element_array(): called on vertex buffer object that is not of type VBT_INDICES", vbb);
		return false;
	}
	if (aab) {
		if (!aab->handle) {
			error("gl_context::set_element_array(): called on not created attribute array binding", aab);
			return false;
		}
	}
	// enable vertex array
	bool not_current = attribute_array_binding_stack.empty() || attribute_array_binding_stack.top() != aab;
	if (aab && not_current)
		glBindVertexArray(get_gl_id(aab->handle));

	// bind buffer to element array
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, get_gl_id(vbb->handle));

	if (aab && not_current)
		glBindVertexArray(attribute_array_binding_stack.empty() ? 0 : get_gl_id(attribute_array_binding_stack.top()->handle));

	return !check_gl_error("gl_context::set_element_array_void()", aab);
}


bool gl_context::set_attribute_array_void(attribute_array_binding_base* aab, int loc, type_descriptor value_type, const vertex_buffer_base* vbb, const void* ptr, size_t nr_elements, unsigned stride) const
{
	if (value_type == ET_MATRIX) {
		error("gl_context::set_attribute_array_void(): called with matrix elements not supported", aab);
		return false;
	}
	if (vbb) {
		if (!vbb->handle) {
			error("gl_context::set_attribute_array_void(): called with not created vertex buffer object", vbb);
			return false;
		}
	}
	if (aab) {
		if (!aab->handle) {
			error("gl_context::set_attribute_array_void(): called on not created attribute array binding", aab);
			return false;
		}
	}

	bool not_current = attribute_array_binding_stack.empty() || attribute_array_binding_stack.top() != aab;
	if (aab && not_current)
		glBindVertexArray(get_gl_id(aab->handle));

	if (vbb)
		glBindBuffer(GL_ARRAY_BUFFER, get_gl_id(vbb->handle));

	bool res = true;
	unsigned n = value_type.element_type == ET_VALUE ? 1 : value_type.nr_rows;
	switch (value_type.coordinate_type) {
	case TI_INT8: value_type.normalize ? glVertexAttribPointer(loc, n, GL_BYTE, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_BYTE, stride, ptr); break;
	case TI_INT16:  value_type.normalize ? glVertexAttribPointer(loc, n, GL_SHORT, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_SHORT, stride, ptr); break;
	case TI_INT32:  value_type.normalize ? glVertexAttribPointer(loc, n, GL_INT, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_INT, stride, ptr); break;
	case TI_UINT8:  value_type.normalize ? glVertexAttribPointer(loc, n, GL_UNSIGNED_BYTE, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_UNSIGNED_BYTE, stride, ptr); break;
	case TI_UINT16: value_type.normalize ? glVertexAttribPointer(loc, n, GL_UNSIGNED_SHORT, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_UNSIGNED_SHORT, stride, ptr); break;
	case TI_UINT32: value_type.normalize ? glVertexAttribPointer(loc, n, GL_UNSIGNED_INT, value_type.normalize, stride, ptr) : glVertexAttribIPointer(loc, n, GL_UNSIGNED_INT, stride, ptr); break;
	case TI_FLT32: glVertexAttribPointer(loc, n, GL_FLOAT, value_type.normalize, stride, ptr); break;
	case TI_FLT64:
		if (GLEW_VERSION_4_1)
			glVertexAttribLPointer(loc, n, GL_DOUBLE, stride, ptr);
		else {
			error("gl_context::set_attribute_array_void(): called with coordinates of type double only supported starting with OpenGL 4.1", aab);
			res = false;
		}
		break;
	default:
		error("gl_context::set_attribute_array_void(): called with unsupported coordinate type", aab);
		res = false;
	}

	if (res)
		glEnableVertexAttribArray(loc);

	if (vbb)
		glBindBuffer(GL_ARRAY_BUFFER, 0);

	if (aab && not_current)
		glBindVertexArray(attribute_array_binding_stack.empty() ? 0 : get_gl_id(attribute_array_binding_stack.top()->handle));


	return res && !check_gl_error("gl_context::set_attribute_array_void()", aab);
}

bool gl_context::enable_attribute_array(attribute_array_binding_base* aab, int loc, bool do_enable) const
{
	bool not_current = attribute_array_binding_stack.empty() || attribute_array_binding_stack.top() != aab;
	if (aab) {
		if (!aab->handle) {
			error("gl_context::enable_attribute_array(): called on not created attribute array binding", aab);
			return false;
		}
		if (not_current)
			glBindVertexArray(get_gl_id(aab->handle));
	}

	if (do_enable)
		glEnableVertexAttribArray(loc);
	else
		glDisableVertexAttribArray(loc);

	if (aab && not_current)
		glBindVertexArray(attribute_array_binding_stack.empty() ? 0 : get_gl_id(attribute_array_binding_stack.top()->handle));

	return !check_gl_error("gl_context::enable_attribute_array()");
}

bool gl_context::is_attribute_array_enabled(const attribute_array_binding_base* aab, int loc) const
{
	bool not_current = attribute_array_binding_stack.empty() || attribute_array_binding_stack.top() != aab;
	if (aab) {
		if (!aab->handle) {
			error("gl_context::is_attribute_array_enabled(): called on not created attribute array binding", aab);
			return false;
		}
		if (not_current)
			glBindVertexArray(get_gl_id(aab->handle));
	}

	GLint res;
	glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &res);

	if (aab && not_current)
		glBindVertexArray(attribute_array_binding_stack.empty() ? 0 : get_gl_id(attribute_array_binding_stack.top()->handle));

	return res == GL_TRUE;
}

GLenum buffer_target(VertexBufferType vbt)
{
	static GLenum buffer_targets[] = { GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_TEXTURE_BUFFER, GL_UNIFORM_BUFFER, GL_TRANSFORM_FEEDBACK_BUFFER };
	return buffer_targets[vbt];
}

GLenum buffer_usage(VertexBufferUsage vbu)
{
	static GLenum buffer_usages[] = { GL_STREAM_DRAW, GL_STREAM_READ, GL_STREAM_COPY, GL_STATIC_DRAW, GL_STATIC_READ, GL_STATIC_COPY, GL_DYNAMIC_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY };
	return buffer_usages[vbu];
}

bool gl_context::vertex_buffer_create(vertex_buffer_base& vbb, const void* array_ptr, size_t size_in_bytes) const
{
	if (!GLEW_VERSION_2_0) {
		error("gl_context::vertex_buffer_create() vertex buffer objects not supported", &vbb);
		return false;
	}
	GLuint b_id;
	glGenBuffers(1, &b_id);
	if (b_id == -1) {
		error(std::string("gl_context::vertex_buffer_create(): ") + gl_error(), &vbb);
		return false;
	}
	vbb.handle = get_handle(b_id);
	glBindBuffer(buffer_target(vbb.type), b_id);
	glBufferData(buffer_target(vbb.type), size_in_bytes, array_ptr, buffer_usage(vbb.usage));
	glBindBuffer(buffer_target(vbb.type), 0);
	return !check_gl_error("gl_context::vertex_buffer_create", &vbb);
}

bool gl_context::vertex_buffer_replace(vertex_buffer_base& vbb, size_t offset, size_t size_in_bytes, const void* array_ptr) const
{
	if (!vbb.handle) {
		error("gl_context::vertex_buffer_replace() vertex buffer object must be created before", &vbb);
		return false;
	}
	GLuint b_id = get_gl_id(vbb.handle);
	glBindBuffer(buffer_target(vbb.type), b_id);
	glBufferSubData(buffer_target(vbb.type), offset, size_in_bytes, array_ptr);
	glBindBuffer(buffer_target(vbb.type), 0);
	return !check_gl_error("gl_context::vertex_buffer_replace", &vbb);
}

bool gl_context::vertex_buffer_copy(const vertex_buffer_base& src, size_t src_offset, vertex_buffer_base& target, size_t target_offset, size_t size_in_bytes) const
{
	if (!src.handle || !target.handle) {
		error("gl_context::vertex_buffer_copy() source and destination vertex buffer objects must have been created before", &src);
		return false;
	}
	GLuint b_id = get_gl_id(src.handle);
	glBindBuffer(GL_COPY_READ_BUFFER, get_gl_id(src.handle));
	glBindBuffer(GL_COPY_WRITE_BUFFER, get_gl_id(target.handle));
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, src_offset, target_offset, size_in_bytes);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	return !check_gl_error("gl_context::vertex_buffer_copy", &src);

}

bool gl_context::vertex_buffer_copy_back(vertex_buffer_base& vbb, size_t offset, size_t size_in_bytes, void* array_ptr) const
{
	if (!vbb.handle) {
		error("gl_context::vertex_buffer_copy_back() vertex buffer object must be created", &vbb);
		return false;
	}
	GLuint b_id = get_gl_id(vbb.handle);
	glBindBuffer(GL_COPY_READ_BUFFER, b_id);
	glGetBufferSubData(GL_COPY_READ_BUFFER, offset, size_in_bytes, array_ptr);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	return !check_gl_error("gl_context::vertex_buffer_copy_back", &vbb);
}

bool gl_context::vertex_buffer_destruct(vertex_buffer_base& vbb) const
{
	if (vbb.handle) {
		GLuint b_id = get_gl_id(vbb.handle);
		glDeleteBuffers(1, &b_id);
		return !check_gl_error("gl_context::vertex_buffer_destruct");
	}
	else {
		error("gl_context::vertex_buffer_destruct(): called on not created vertex buffer", &vbb);
		return false;
	}
}


		}
	}
}