/*
 * vlc-vout-clutter.
 *
 * Clutter integration library for VLC.
 *
 * Authored By Arnaud Vallat  <rno.rno@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#define DOMAIN  "vlc-myplugin"
#define _(str)  dgettext(DOMAIN, str)
#define N_(str) (str)

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout.h>

#include <clutter/clutter.h>
#include <cogl/cogl.h>

static int vlc_vout_clutter_create(vlc_object_t* vlc_object);
static void vlc_vout_clutter_destroy(vlc_object_t* vlc_object);

vlc_module_begin ()
    set_description( N_( "clutter output" ) )
    set_shortname( N_("clutter output") )

    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VOUT )
    set_capability( "video output", 0 )

    add_string( "clutter-texture", "0", NULL, N_("ClutterTexture"), N_("Clutter texture used to render video."), true )

    set_callbacks( vlc_vout_clutter_create, vlc_vout_clutter_destroy )
vlc_module_end ()

struct vout_sys_t
{
  volatile guint ref_count;

  volatile guint buffer_ready;

  volatile guint idle_id;

  unsigned char* buffer;

  ClutterTexture* texture;
  guint texture_width;
  guint texture_height;
  guint texture_bpp;
};


static gboolean vlc_vout_clutter_update_frame(gpointer data)
{
  vout_thread_t* vout_thread;

  vout_thread = data;

  while (g_atomic_int_get(&vout_thread->p_sys->buffer_ready) == 0)
    ;

  if (vout_thread->p_sys->texture && vout_thread->p_sys->buffer)
    {
      clutter_texture_set_from_rgb_data(vout_thread->p_sys->texture,
					(const guchar *)vout_thread->p_sys->buffer,
					FALSE,
					vout_thread->p_sys->texture_width,
					vout_thread->p_sys->texture_height,
					vout_thread->p_sys->texture_width * vout_thread->p_sys->texture_bpp,
					vout_thread->p_sys->texture_bpp,
					CLUTTER_TEXTURE_RGB_FLAG_BGR,
					NULL);
    }

  g_atomic_int_set(&vout_thread->p_sys->buffer_ready, 0);
  g_atomic_int_set(&vout_thread->p_sys->idle_id, 0);
  g_atomic_int_add(&vout_thread->p_sys->ref_count, -1);

  return FALSE;
}


static int vlc_vout_clutter_init(vout_thread_t* vout_thread)
{
  char* tmp;

  tmp = var_CreateGetString(vout_thread, "clutter-texture");

  vout_thread->p_sys->texture = (ClutterTexture *)(intptr_t)atoll(tmp);
  free(tmp);

  if (vout_thread->p_sys->texture == NULL)
    {
      msg_Err(vout_thread, "clutter-texture is missing. Unable to init clutter driver");
      return VLC_EGENERIC;
    }

  /* race here
   */
  g_object_ref(vout_thread->p_sys->texture);

  /* TODO: may change according to cogl support
   */
  vout_thread->output.i_chroma = VLC_FOURCC('R', 'V', '2', '4');
  vout_thread->output.i_rmask = 0xff0000;
  vout_thread->output.i_gmask = 0x00ff00;
  vout_thread->output.i_bmask = 0x0000ff;
  vout_thread->p_sys->texture_bpp = 3;

  vout_thread->p_sys->texture_width = vout_thread->render.i_width;
  vout_thread->p_sys->texture_height = vout_thread->render.i_height;

  vout_thread->output.i_width = vout_thread->p_sys->texture_width;
  vout_thread->output.i_height = vout_thread->p_sys->texture_height;
  vout_thread->output.i_aspect = vout_thread->output.i_width
    * VOUT_ASPECT_FACTOR / vout_thread->output.i_height;

  vout_thread->p_sys->buffer = malloc(vout_thread->p_sys->texture_width *
				      vout_thread->p_sys->texture_height *
				      vout_thread->p_sys->texture_bpp);
  if (vout_thread->p_sys->buffer == NULL)
    {
      g_object_unref(vout_thread->p_sys->texture);
      vout_thread->p_sys->texture = NULL;

      msg_Err(vout_thread, "Unable to allocate buffer[0]");
      return VLC_ENOMEM;
    }

  vout_thread->p_picture[0].i_planes = 1;
  vout_thread->p_picture[0].p->p_pixels = vout_thread->p_sys->buffer;
  vout_thread->p_picture[0].p->i_lines = vout_thread->output.i_height;
  vout_thread->p_picture[0].p->i_visible_lines = vout_thread->output.i_height;
  vout_thread->p_picture[0].p->i_pixel_pitch = vout_thread->p_sys->texture_bpp;
  vout_thread->p_picture[0].p->i_pitch = vout_thread->output.i_width * vout_thread->p_picture[0].p->i_pixel_pitch;
  vout_thread->p_picture[0].p->i_visible_pitch = vout_thread->output.i_width * vout_thread->p_picture[0].p->i_pixel_pitch;
  vout_thread->p_picture[0].i_status = DESTROYED_PICTURE;
  vout_thread->p_picture[0].i_type = DIRECT_PICTURE;

  /* may add lock / unlock routines to picture
   */

  vout_thread->output.pp_picture[0] = &vout_thread->p_picture[0];
  vout_thread->output.i_pictures = 1;

  return VLC_SUCCESS;
}


static void vlc_vout_clutter_end(vout_thread_t* vout_thread)
{
  gint try;

  for (try = 0; try < 3; ++try)
    if (g_atomic_int_get(&vout_thread->p_sys->ref_count) == 1)
      break;

  if (g_atomic_int_get(&vout_thread->p_sys->ref_count) > 1
      && g_atomic_int_get(&vout_thread->p_sys->idle_id) != 0)
    {
      g_source_remove(vout_thread->p_sys->idle_id);
      vout_thread->p_sys->idle_id = 0;
    }

  if (vout_thread->p_sys->buffer)
    {
      free(vout_thread->p_sys->buffer);
      vout_thread->p_sys->buffer = NULL;
    }

  if (vout_thread->p_sys->texture)
    {
      g_object_unref(vout_thread->p_sys->texture);
      vout_thread->p_sys->texture = NULL;
    }
}


static void vlc_vout_clutter_display(vout_thread_t* vout_thread, picture_t* picture)
{
  if (g_atomic_int_get(&vout_thread->p_sys->idle_id) != 0)
    return;

  g_atomic_int_add(&vout_thread->p_sys->ref_count, 1);

  g_atomic_int_set(&vout_thread->p_sys->idle_id,
		   clutter_threads_add_idle_full(G_PRIORITY_HIGH_IDLE,
						 vlc_vout_clutter_update_frame,
						 vout_thread,
						 NULL));

  g_atomic_int_set(&vout_thread->p_sys->buffer_ready, 1);
}


static int vlc_vout_clutter_create(vlc_object_t* vlc_object)
{
  vout_thread_t* vout_thread;

  vout_thread = (vout_thread_t *)vlc_object;

  vout_thread->p_sys = malloc(sizeof(vout_sys_t));
  if (vout_thread->p_sys == NULL)
    return VLC_ENOMEM;

  memset(vout_thread->p_sys, 0, sizeof(vout_sys_t));

  vout_thread->p_sys->ref_count = 1;

  vout_thread->pf_init = vlc_vout_clutter_init;
  vout_thread->pf_end = vlc_vout_clutter_end;
  vout_thread->pf_display = vlc_vout_clutter_display;

  return VLC_SUCCESS;
}


static void vlc_vout_clutter_destroy(vlc_object_t* vlc_object)
{
  vout_thread_t* vout_thread;

  vout_thread = (vout_thread_t *)vlc_object;

  free(vout_thread->p_sys);
  vout_thread->p_sys = NULL;
}
