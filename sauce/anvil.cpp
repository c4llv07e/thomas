#ifdef __linux__
#define SOKOL_GLCORE33
#elif _WIN32
#define SOKOL_D3D11
#endif
#define SOKOL_IMPL
#include "ext/sokol_gfx.h"
#include "ext/sokol_gp.h"
#include "ext/sokol_app.h"
#include "ext/sokol_glue.h"

#define HANDMADE_MATH_IMPLEMENTATION
#include "ext/HandmadeMath.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ext/stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

#include "thomas.h"
#include "anvil.h"

static void frame(void) {
	GameState* gs = game_state();
	WorldState* world = world_state();
	if (gs->key_pressed[SAPP_KEYCODE_B]) {
		MEMORY_ZERO_STRUCT(world);
		th_world_init(world);
	}

	Entity*& player = world->player;
	const vec2 window_size = { (float)sapp_width(), (float)sapp_height() };
	gs->window_size = window_size;
	float ratio = window_size.x / (float)window_size.y;
	float delta_t = sapp_frame_duration();
	const vec2 world_mouse = mouse_pos_in_worldspace();

	// PLAYER INPUT
	if (world->player) {
		if (gs->key_pressed[SAPP_KEYCODE_SPACE]) {
			player->vel.y = 300.0f;
		}
		vec2 axis_input = { 0 };
		if (gs->key_down[SAPP_KEYCODE_A]) {
			axis_input.x -= 1.0f;
			world->player->flip_horizontal = 1;
		}
		if (gs->key_down[SAPP_KEYCODE_D]) {
			axis_input.x += 1.0f;
			world->player->flip_horizontal = 0;
		}
		player->acc = axis_input * MOVE_SPEED;
	}

	// Particle Emit
	for (int i = 0; i < gs->emitter_count; i++) {
		Emitter* emitter = &gs->emitters[i];
		
		float freq_remainder = emitter->frequency - floorf(emitter->frequency);
		bool8 remainder = 0;
		if (((float)rand() / (float)RAND_MAX) < freq_remainder)
			remainder = 1;

		uint32 emit_amount = floorf(emitter->frequency) + remainder;
		for (int j = 0; j < emit_amount; j++) {
			gs->particle_count++;
			if (gs->particle_count == ArrayCount(gs->particles))
				gs->particle_count = 0;
			Particle* new_particle = &gs->particles[gs->particle_count];
			if (!float_is_zero(new_particle->life))
				LOG("warning: particles are being overridden");
			emitter->emit_func(new_particle, emitter);
		}
	}

	// Entity Physics
	for (int i = 0; i < world->entity_count; i++) {
		Entity* entity = &world->entities[i];
		if (!entity->rigid_body)
			continue;

		// acc counter force with existing velocity
		entity->acc.x += -15.0f * entity->vel.x;

		// gravity
		bool8 falling = entity->vel.y < 0.f;
		entity->acc.y -= (falling ? 2.f : 1.f) * GRAVITY;

		// integrate acceleration and velocity into position
		vec2 next_pos = 0.5f * entity->acc * SQUARE(delta_t) + entity->vel * delta_t + entity->pos;

		// integrate acceleration into velocity
		entity->vel += entity->acc * delta_t;
		entity->acc.x = 0;
		entity->acc.y = 0;

		if (next_pos.y < 0.0f) {
			next_pos.y = 0.0f;
			entity->vel.y = 0.0f;
		}

		entity->pos = next_pos;
	}

	// update camera
	if (world->player) {
		gs->cam.pos.x = player->pos.x;
		gs->cam.pos.y = 20.0f;
	}

	// BEGIN RENDER
	sgp_begin(window_size.x, window_size.y);
	sgp_viewport(0, 0, window_size.x, window_size.y);

	#if 1
	sgp_project(window_size.x * -0.5f, window_size.x * 0.5f, window_size.y * 0.5f, window_size.y * -0.5f);
	sgp_scale(gs->cam.scale, gs->cam.scale);
	sgp_translate(-gs->cam.pos.x, -gs->cam.pos.y);
	#else
	range2 view_rect = { 0 };
	view_rect.max = vec2(window_size.x, window_size.y);
	view_rect = range2_center_middle(view_rect);
	view_rect = range2_shift(view_rect, vec2(0, 100.f));
	view_rect = range2_scale(view_rect, gs->cam.scale);
	view_rect = range2_shift(view_rect, gs->cam.pos);
	sgp_project(view_rect.min.x, view_rect.max.x, view_rect.max.y, view_rect.min.y);
	#endif

	// @sgp_helpers - vector expander, so I can use my types
	sgp_set_blend_mode(SGP_BLENDMODE_BLEND);
	sgp_set_color(0.1f, 0.1f, 0.1f, 1.0f);
	sgp_clear();

	// SEED
	if (world->held_seed) {
		Entity* seed = world->held_seed;
		seed->pos.x = roundf(world_mouse.x);
		seed->pos.y = 0.0f;

		sgp_set_color(1.0f, 1.0f, 1.0f, 1.0f);
		sgp_draw_line(seed->pos.x, world_mouse.y, seed->pos.x, 0.0f);

		if (gs->mouse_pressed[SAPP_MOUSEBUTTON_LEFT]) {
			Entity* plant = th_entity_create_plant();
			plant->pos = seed->pos;
		}
	}

	// PLANT UPDATE
	for (int i = 0; i < world->entity_count; i++) {
		Entity* entity = &world->entities[i];
		if (!entity->plant)
			continue;

		entity->plant_stage += delta_t * 4.f;
		if (entity->plant_stage > 7.999f) {
			entity->plant_stage = 8.0f;
		}

		uint8 stage = floorf(entity->plant_stage);
		stage = CLAMP_UPPER(stage, 6);
		Sprite* plant = th_texture_sprite_get("plant0");
		plant += stage;
		entity->render_rects[0].sprite = plant;
	}

	// Particle Render
	for (int i = 0; i < ArrayCount(gs->particles); i++) {
		Particle* particle = &gs->particles[i];
		if (float_is_zero(particle->life))
			continue;

		Assert(particle->life > 0.f);
		particle->life -= delta_t;
		if (particle->life <= 0.f) {
			MEMORY_ZERO_STRUCT(particle);
			continue;
		}

		particle->pos += particle->vel * delta_t;

		float alpha = float_alpha(particle->life, particle->start_life, 0.f);
		alpha = float_alpha_sin_mid(alpha);

		DEFER_LOOP(sgp_push_transform(), sgp_pop_transform()) {
			vec2 render_size = vec2{1, 1} * particle->size_mult;
			sgp_set_color(particle->col.r, particle->col.g, particle->col.b, particle->col.a * alpha);
			sgp_translate(particle->pos.x, particle->pos.y);
			sgp_draw_filled_rect(render_size.x * -0.5f, render_size.y * -0.5f, render_size.x, render_size.y);
		}
	}

	sgp_set_color(1.0f, 1.0f, 1.0f, 1.0f);
	sgp_draw_line(-200.f, 0.0f, 200.0f, 0.0f);

	// Entity Render
	for (int i = 0; i < world->entity_count; i++) {
		Entity* entity = &world->entities[i];
		if (!entity->render)
			continue;

		for (int j = 0; j < entity->render_rect_count; j++) {
			RenderRect* render = &entity->render_rects[j];
			
			range2 rect = render->rect;
			if (entity->flip_horizontal) {
				rect.min.x *= -1.0f;
				rect.max.x *= -1.0f;
				float temp = rect.min.x;
				rect.min.x = rect.max.x;
				rect.max.x = temp;
			}
			rect = range2_shift(rect, entity->pos);

			// draw rect
			sgp_set_color(V4_EXPAND(render->col));
			if (render->sprite) {
				Assert(render->sprite->atlas); // invalid atlas
				sgp_set_image(0, render->sprite->atlas->image);
				sgp_draw_textured_rect_ex(0, range2_to_sgp_rect(rect), range2_to_sgp_rect(render->sprite->sub_rect));
				sgp_unset_image(0);
			} else {
				// this is the price you pay for using other people's libraries (a worthwhile trade off in this case (for now))
				sgp_rect icky_rect = range2_to_sgp_rect(rect);
				sgp_draw_filled_rect(icky_rect.x, icky_rect.y, icky_rect.w, icky_rect.h);
			}
		}
	}

	#ifdef RENDER_COLLIDERS
	for (int i = 0; i < world->entities.count; i++) {
		Entity* entity = &world->entities[i];
		if (!entity->rigid_body)
			continue;
		range2 rect = entity->bounds;
		rect = range2_shift(rect, entity->pos);
		sgp_set_color(RENDER_COLLIDER_COLOR);
		sgp_draw_line(rect.min.x, rect.min.y, rect.min.x, rect.max.y);
		sgp_draw_line(rect.min.x, rect.min.y, rect.max.x, rect.min.y);
		sgp_draw_line(rect.max.x, rect.max.y, rect.min.x, rect.max.y);
		sgp_draw_line(rect.max.x, rect.max.y, rect.max.x, rect.min.y);
	}
	#endif

	sg_pass_action pass_action = { 0 };
	sg_begin_default_pass(&pass_action, window_size.x, window_size.y);
	sgp_flush();
	sgp_end();
	sg_end_pass();
	sg_commit();

	// clear press and release events
	memset(&gs->key_pressed, 0, sizeof(gs->key_pressed));
	memset(&gs->key_released, 0, sizeof(gs->key_released));
	memset(&gs->mouse_pressed, 0, sizeof(gs->mouse_pressed));
	memset(&gs->mouse_released, 0, sizeof(gs->mouse_released));
}

static void init(void) {
	sg_desc sgdesc = { .context = sapp_sgcontext() };
	sg_setup(&sgdesc);
	if (!sg_isvalid()) {
		fprintf(stderr, "Failed to create Sokol GFX context!\n");
		exit(-1);
	}

	sgp_desc sgpdesc = { 0 };
	sgp_setup(&sgpdesc);
	if (!sgp_is_valid()) {
		fprintf(stderr, "Failed to create Sokol GP context: %s\n", sgp_get_error_message(sgp_get_last_error()));
		exit(-1);
	}

	// ENTRY
	GameState* gs = game_state();
	WorldState* world = world_state();
	Atlas* plants = th_texture_atlas_load("plant.png");
	range2 plant_sub_rect = range2(vec2(), vec2(16, 64));
	th_texture_sprite_create(plants, "plant0", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant1", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant2", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant3", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant4", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant5", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant6", plant_sub_rect);
	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	th_texture_sprite_create(plants, "plant7", plant_sub_rect);

	plant_sub_rect = range2_shift(plant_sub_rect, vec2(16, 0));
	plant_sub_rect.max = plant_sub_rect.min;
	plant_sub_rect.max += vec2(4, 4);
	th_texture_sprite_create(plants, "resource1", plant_sub_rect);
	{
		// background emitter
		Emitter* emitter = TH_ARRAY_PUSH(gs->emitters, gs->emitter_count);
		emitter->emit_func = emitter_ambient_screen;
		emitter->frequency = 10.0f;
	}

	gs->cam.scale = DEFAULT_CAMERA_SCALE;

	th_world_init(world);
}

static void cleanup(void) {
	sgp_shutdown();
	sg_shutdown();
}

static void event(const sapp_event* ev) {
	GameState* gs = game_state();
	switch (ev->type) {
	case SAPP_EVENTTYPE_KEY_UP: {
		bool8 already_up = !gs->key_down[ev->key_code];
		if (!already_up)
			gs->key_released[ev->key_code] = 1;
		gs->key_down[ev->key_code] = 0;
	} break;
	case SAPP_EVENTTYPE_KEY_DOWN: {
		bool8 already_down = gs->key_down[ev->key_code];
		if (!already_down)
			gs->key_pressed[ev->key_code] = 1;
		gs->key_down[ev->key_code] = 1;
	} break;
	case SAPP_EVENTTYPE_MOUSE_UP: {
		bool8 already_up = !gs->mouse_down[ev->mouse_button];
		if (!already_up)
			gs->mouse_released[ev->mouse_button] = 1;
		gs->mouse_down[ev->mouse_button] = 0;
	} break;
	case SAPP_EVENTTYPE_MOUSE_DOWN: {
		bool8 already_down = gs->mouse_down[ev->mouse_button];
		if (!already_down)
			gs->mouse_pressed[ev->mouse_button] = 1;
		gs->mouse_down[ev->mouse_button] = 1;
	} break;
	case SAPP_EVENTTYPE_MOUSE_SCROLL: {
		gs->cam.scale += ev->scroll_y / 8.0f;
		gs->cam.scale = CLAMP(1.0f, gs->cam.scale, 10.0f);
	} break;
	case SAPP_EVENTTYPE_MOUSE_MOVE: {
		gs->mouse_pos = vec2{ev->mouse_x, ev->mouse_y};
	};
	}

	#ifdef FUN_VAL
	fun_val = float_map(ev->mouse_y, 0, gs->window_size.y, -10.f, 10.f);
	fun_val *= float_map(ev->mouse_x, 0, gs->window_size.x, 0.00001f, 100.f);
	LOG("%f", fun_val);
	#endif
}

sapp_desc sokol_main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	sapp_desc test = {
		.init_cb = init,
		.frame_cb = frame,
		.cleanup_cb = cleanup,
		.event_cb = event,
		.sample_count = 2,
		.window_title = APP_NAME,
	};
	return test;
}
