/*
 * (c) Copyright 2004-2006 Mitsubishi Electric Corp.
 *
 * All rights reserved.
 *
 * Written by Koichi Hiramatsu,
 *            Seishi Takahashi,
 *            Atsushi Hori
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include <pthread.h>

#include <fusion/shmalloc.h>

#include <directfb_arib.h>

#include <core/coredefs.h>
#include <core/coretypes.h>

#include <core/core.h>
#include <core/layers.h>
#include <core/layer_context.h>
#include <core/gfxcard.h>
#include <core/input.h>
#include <core/palette.h>
#include <core/state.h>
#include <core/system.h>
#include <core/windows.h>
#include <core/windowstack.h>
#include <core/wm.h>

#include <misc/conf.h>
#include <misc/util.h>

#include <direct/mem.h>
#include <direct/messages.h>
#include <direct/util.h>

#include <gfx/util.h>

#include <core/layers_internal.h>
#include <core/windows_internal.h>

static DFBResult
create_region( CoreDFB                *core,
               CoreLayerContext       *context,
               CoreWindow             *window,
               DFBSurfacePixelFormat  format,
               DFBSurfaceCapabilities surface_caps,
               CoreLayerRegion        **ret_region,
               CoreSurface            **ret_surface )
{
     DFBResult             ret;
     CoreLayerRegionConfig config;
     CoreLayerRegion       *region;
     CoreSurface           *surface;

     D_ASSERT( core != NULL );
     D_ASSERT( context != NULL );
     D_ASSERT( window != NULL );
     D_ASSERT( ret_region != NULL );
     D_ASSERT( ret_surface != NULL );

     memset( &config, 0, sizeof(CoreLayerRegionConfig) );

     config.width      = window->config.bounds.w;
     config.height     = window->config.bounds.h;
     config.format     = format;
     config.buffermode = DLBM_BACKVIDEO;
     config.options    = context->config.options & DLOP_FLICKER_FILTERING;
     config.source     = (DFBRectangle) { 0, 0, config.width, config.height };
     config.dest       = window->config.bounds;
     config.opacity    = 0;

     if ((context->config.options & DLOP_ALPHACHANNEL) && DFB_PIXELFORMAT_HAS_ALPHA(format))
          config.options |= DLOP_ALPHACHANNEL;

     config.options |= DLOP_OPACITY;


     ret = dfb_layer_region_create( context, &region );
     if (ret)
          return ret;


     do {
          ret = dfb_layer_region_set_configuration( region, &config, CLRCF_ALL );
          if (ret) {
               if (config.options & DLOP_OPACITY)
                    config.options &= ~DLOP_OPACITY;
               else if (config.options & DLOP_ALPHACHANNEL)
                    config.options = (config.options & ~DLOP_ALPHACHANNEL) | DLOP_OPACITY;
               else {
                    D_ERROR( "DirectFB/Core/Windows: Unable to set region configuration!\n" );
                    dfb_layer_region_unref( region );
                    return ret;
               }
          }
     } while (ret);


     ret = dfb_surface_create( core, config.width, config.height, format,
                               CSP_VIDEOONLY, surface_caps | DSCAPS_DOUBLE, NULL, &surface );
     if (ret) {
          dfb_layer_region_unref( region );
          return ret;
     }

     ret = dfb_layer_region_set_surface( region, surface );
     if (ret) {
          dfb_surface_unref( surface );
          dfb_layer_region_unref( region );
          return ret;
     }

     ret = dfb_layer_region_enable( region );
     if (ret) {
          dfb_surface_unref( surface );
          dfb_layer_region_unref( region );
          return ret;
     }

     *ret_region  = region;
     *ret_surface = surface;

     return DFB_OK;
}

DFBResult
dfb_arib_window_create( CoreWindowStack        *stack,
                        DFBWindowID            arib_id,
                        int                    x,
                        int                    y,
                        int                    width,
                        int                    height,
                        DFBWindowCapabilities  caps,
                        DFBSurfaceCapabilities surface_caps,
                        DFBSurfacePixelFormat  pixelformat,
                        CoreWindow             **ret_window )
{
     DFBResult         ret;
     CoreSurface       *surface;
     CoreSurfacePolicy surface_policy = CSP_SYSTEMONLY;
     CoreLayer         *layer;
     CoreLayerContext  *context;
     CoreWindow        *window;
     CardCapabilities  card_caps;
     CoreWindowConfig  config;

     D_ASSERT( stack != NULL );
     D_ASSERT( stack->context != NULL );
     D_ASSERT( width > 0 );
     D_ASSERT( height > 0 );
     D_ASSERT( ret_window != NULL );

     A_TRACE("%s: (%d)(%d,%d,%d,%d)\n", __FUNCTION__, arib_id, x, y, width, height);

     ret = dfb_wm_arib_check_window_id( stack, arib_id, ret_window );
     if (ret) {
          A_TRACE("%s: dfb_wm_arib_check_window_id=%d\n", __FUNCTION__, ret );
          return ret;
     }

     if (width > 4096 || height > 4096)
          return DFB_LIMITEXCEEDED;

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     context = stack->context;
     layer   = dfb_layer_at( context->layer_id );

     surface_caps &= DSCAPS_INTERLACED | DSCAPS_SEPARATED | DSCAPS_DEPTH |
                     DSCAPS_STATIC_ALLOC | DSCAPS_SYSTEMONLY | DSCAPS_VIDEOONLY;

     if (!dfb_config->translucent_windows) {
          caps &= ~DWCAPS_ALPHACHANNEL;

          /*if (DFB_PIXELFORMAT_HAS_ALPHA(pixelformat))
               pixelformat = DSPF_UNKNOWN;*/
     }

     /* Choose pixel format. */
     if (caps & DWCAPS_ALPHACHANNEL) {
          if (pixelformat == DSPF_UNKNOWN)
               pixelformat = DSPF_ARGB;
          else if (! DFB_PIXELFORMAT_HAS_ALPHA(pixelformat)) {
               dfb_windowstack_unlock( stack );
               return DFB_INVARG;
          }
     }
     else if (pixelformat == DSPF_UNKNOWN) {
          if (context->config.flags & DLCONF_PIXELFORMAT)
               pixelformat = context->config.pixelformat;
          else {
               D_WARN( "layer config has no pixel format, using RGB16" );

               pixelformat = DSPF_RGB16;
          }
     }

     /* Choose window surface policy */
     if ((surface_caps & DSCAPS_VIDEOONLY) ||
         (context->config.buffermode == DLBM_WINDOWS))
     {
          surface_policy = CSP_VIDEOONLY;
     }
     else if (!(surface_caps & DSCAPS_SYSTEMONLY) &&
              context->config.buffermode != DLBM_BACKSYSTEM)
     {
          if (dfb_config->window_policy != -1) {
               /* Use the explicitly specified policy. */
               surface_policy = dfb_config->window_policy;
          }
          else {
               /* Examine the hardware capabilities. */
               dfb_gfxcard_get_capabilities( &card_caps );

               if (card_caps.accel & DFXL_BLIT) {
                    if ((card_caps.blitting & DSBLIT_BLEND_ALPHACHANNEL) ||
                        !(caps & DWCAPS_ALPHACHANNEL))
                         surface_policy = CSP_VIDEOHIGH;
               }
          }
     }

     surface_caps &= ~(DSCAPS_SYSTEMONLY | DSCAPS_VIDEOONLY);

     switch (surface_policy) {
          case CSP_SYSTEMONLY:
               surface_caps |= DSCAPS_SYSTEMONLY;
               break;

          case CSP_VIDEOONLY:
               surface_caps |= DSCAPS_VIDEOONLY;
               break;

          default:
               break;
     }

     if (caps & DWCAPS_DOUBLEBUFFER)
          surface_caps |= DSCAPS_DOUBLE;

     memset( &config, 0, sizeof(CoreWindowConfig) );

     config.bounds.x = x;
     config.bounds.y = y;
     config.bounds.w = width;
     config.bounds.h = height;
     config.events   = DWET_ALL;

     /* Auto enable blending for ARGB ,LUT8, DSPF_AYCbCr, DSPF_LUT8AYCbCr */
     if (caps & DWCAPS_ALPHACHANNEL) {
          if ((pixelformat == DSPF_ARGB)
          ||  (pixelformat == DSPF_LUT8)
          ||  (pixelformat == DSPF_AYCbCr)
          ||  (pixelformat == DSPF_LUT8AYCbCr)) {
               config.options |= DWOP_ALPHACHANNEL;
          }
     }

     /* Create the window object. */
     window = dfb_core_create_window( layer->core );

     window->id      = ++stack->id_pool;
     window->arib_id = arib_id;
     window->caps    = caps;
     window->stack   = stack;
     window->config  = config;

     /* Create the window's surface using the layer's palette if possible. */
     if (! (caps & DWCAPS_INPUTONLY)) {
          if (context->config.buffermode == DLBM_WINDOWS) {
               CoreLayerRegion *region;

               /* Create a region for the window. */
               ret = create_region( layer->core, context, window,
                                    pixelformat, surface_caps, &region, &surface );
               if (ret) {
                    fusion_object_destroy( &window->object );
                    dfb_windowstack_unlock( stack );
                    return ret;
               }

               /* Link the region into the window structure. */
               dfb_layer_region_link( &window->region, region );
               dfb_layer_region_unref( region );

               /* Link the surface into the window structure. */
               dfb_surface_link( &window->surface, surface );
               dfb_surface_unref( surface );

               /* Attach our global listener to the surface. */
               dfb_surface_attach_global( surface, DFB_WINDOW_SURFACE_LISTENER,
                                          window, &window->surface_reaction );
          }
          else {
               CoreLayerRegion *region;

               /* Get the primary region of the layer context. */
               ret = dfb_layer_context_get_primary_region( context, true, &region );
               if (ret) {
                    fusion_object_destroy( &window->object );
                    dfb_windowstack_unlock( stack );
                    return ret;
               }

               /* Link the primary region into the window structure. */
               dfb_layer_region_link( &window->primary_region, region );
               dfb_layer_region_unref( region );

               /* Create the surface for the window. */
               ret = dfb_surface_create( layer->core,
                                         width, height, pixelformat,
                                         surface_policy, surface_caps,
                                         region->surface ?
                                         region->surface->palette : NULL, &surface );
               if (ret) {
                    dfb_layer_region_unlink( &window->primary_region );
                    fusion_object_destroy( &window->object );
                    dfb_windowstack_unlock( stack );
                    return ret;
               }

               /* Link the surface into the window structure. */
               dfb_surface_link( &window->surface, surface );
               dfb_surface_unref( surface );

               /* Attach our global listener to the surface. */
               dfb_surface_attach_global( surface, DFB_WINDOW_SURFACE_LISTENER,
                                          window, &window->surface_reaction );
          }
     }

     /* Pass the new window to the window manager. */
     ret = dfb_wm_add_window( stack, window );
     if (ret) {
          if (window->surface) {
               dfb_surface_detach_global( surface, &window->surface_reaction );
               dfb_surface_unlink( &window->surface );
          }

          if (window->primary_region)
               dfb_layer_region_unlink( &window->primary_region );

          if (window->region)
               dfb_layer_region_unlink( &window->region );

          fusion_object_destroy( &window->object );
          dfb_windowstack_unlock( stack );
          return ret;
     }

     /* Indicate that initialization is complete. */
     D_FLAGS_SET( window->flags, CWF_INITIALIZED );

     /* Increase number of windows. */
     stack->num++;

     /* Finally activate the object. */
     fusion_object_activate( &window->object );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     /* Return the new window. */
     *ret_window = window;

     return DFB_OK;;
}

DFBResult
dfb_arib_window_get( CoreWindowStack *stack,
                     DFBWindowID     arib_id,
                     CoreWindow      **ret_window )
{
     DFBResult ret;

     D_ASSERT( stack != NULL );
     D_ASSERT( stack->context != NULL );
     D_ASSERT( ret_window != NULL );

     A_TRACE("%s: (%d)\n", __FUNCTION__, arib_id);

     ret = dfb_wm_arib_check_window_id( stack, arib_id, ret_window );
     if (ret != DFB_BUSY) {
          A_TRACE("%s: dfb_wm_arib_check_window_id=%d Not Created\n", __FUNCTION__, ret );
     }
     return ret;
}


DFBResult
dfb_arib_window_repaint( CoreWindow          *window,
                         DFBRegion           *region,
                         DFBSurfaceFlipFlags flags )
{
     DFBResult       ret;
     CoreWindowStack *stack = window->stack;

     D_ASSERT( window != NULL );
     D_ASSERT( window->stack != NULL );

     DFB_REGION_ASSERT_IF( region );

     A_TRACE("%s: region(%d,%d,%d,%d) flags(%d)\n", __FUNCTION__,
 			region->x1,	region->y1,	region->x2,	region->y2,flags);

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     ret = dfb_wm_arib_update_window( window, region, flags );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}


DFBResult
dfb_arib_window_set_switching_region( CoreWindow   *window,
                                      DFBRectangle *rect1,
                                      DFBRectangle *rect2,
                                      DFBRectangle *rect3,
                                      DFBRectangle *rect4,
                                      DFBBoolean   attribute )
{
	CoreWindowStack *stack = window->stack;

	D_ASSERT( window != NULL );
	D_ASSERT( window->stack != NULL );

     A_TRACE("%s:\n", __FUNCTION__);

	/* Lock the window stack. */
	if (dfb_windowstack_lock( stack ))
	  return DFB_FUSION;

	if (window->arib_id == DARIBWID_STILLPICTURE_PLANE) {

		dfb_wm_arib_set_switching_region( window, rect1, rect2, rect3, rect4, attribute );
	}

	/* Unlock the window stack. */
	dfb_windowstack_unlock( stack );

	return DFB_OK;
}

DFBResult
dfb_arib_window_change_events( CoreWindow         *window,
                               DFBWindowEventType disable,
                               DFBWindowEventType enable )
{
     DFBResult        ret;
     CoreWindowConfig config;
     CoreWindowStack  *stack = window->stack;

     D_ASSUME( disable | enable );

     A_TRACE("%s:\n", __FUNCTION__);

     if (!disable && !enable)
          return DFB_OK;

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     config.events = (window->config.events & ~disable) | enable;

     ret = dfb_wm_arib_set_window_config( window, &config, CWCF_EVENTS );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}

DFBResult
dfb_arib_window_set_colorkey( CoreWindow *window,
                              __u32       color_key )
{
     DFBResult        ret;
     CoreWindowConfig config;
     CoreWindowStack  *stack = window->stack;

     A_TRACE("%s:\n", __FUNCTION__);

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     if (window->config.color_key == color_key) {
          dfb_windowstack_unlock( stack );
          return DFB_OK;
     }

     config.color_key = color_key;

     ret = dfb_wm_arib_set_window_config( window, &config, CWCF_COLOR_KEY );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}

DFBResult
dfb_arib_window_change_options( CoreWindow       *window,
                                DFBWindowOptions disable,
                                DFBWindowOptions enable )
{
     DFBResult        ret;
     CoreWindowConfig config;
     CoreWindowStack  *stack = window->stack;

     D_ASSUME( disable | enable );

     A_TRACE("%s:\n", __FUNCTION__);

     if (!disable && !enable)
          return DFB_OK;

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     config.options = (window->config.options & ~disable) | enable;

     ret = dfb_wm_arib_set_window_config( window, &config, CWCF_OPTIONS );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}

DFBResult
dfb_arib_window_set_opaque( CoreWindow      *window,
                            const DFBRegion *region )
{
     DFBResult        ret;
     CoreWindowConfig config;
     CoreWindowStack  *stack = window->stack;

     DFB_REGION_ASSERT_IF( region );

     A_TRACE("%s:\n", __FUNCTION__);

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     config.opaque.x1 = 0;
     config.opaque.y1 = 0;
     config.opaque.x2 = window->config.bounds.w - 1;
     config.opaque.y1 = window->config.bounds.h - 1;

     if (region && !dfb_region_region_intersect( &config.opaque, region ))
          ret = DFB_INVAREA;
     else
          ret = dfb_wm_arib_set_window_config( window, &config, CWCF_OPAQUE );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}

DFBResult
dfb_arib_window_set_opacity( CoreWindow *window,
                             __u8       opacity )
{
     DFBResult        ret;
     CoreWindowConfig config;
     CoreWindowStack  *stack = window->stack;

     A_TRACE("%s: opacity(%d)\n", __FUNCTION__, opacity);

     /* Lock the window stack. */
     if (dfb_windowstack_lock( stack ))
          return DFB_FUSION;

     if (window->config.opacity == opacity) {
          dfb_windowstack_unlock( stack );
          return DFB_OK;
     }

     config.opacity = opacity;

     ret = dfb_wm_arib_set_window_config( window, &config, CWCF_OPACITY );

     /* Unlock the window stack. */
     dfb_windowstack_unlock( stack );

     return ret;
}

DFBResult
dfb_arib_window_batch_start( CoreWindow *window )
{
	CoreWindowStack *stack = window->stack;

	/* Lock the window stack. */
	if (dfb_windowstack_lock( stack ))
		return DFB_FUSION;

	stack->batch_count++;

	dfb_windowstack_unlock( stack );

	return DFB_OK;
}

DFBResult
dfb_arib_window_batch_end( CoreWindow *window )
{
	CoreWindowStack *stack = window->stack;
	DFBRegion       region;

	/* Lock the window stack. */
	if (dfb_windowstack_lock( stack ))
		return DFB_FUSION;

	stack->batch_count--;

	if (!stack->batch_count) {
		if (stack->modify_rect.w > 0 && stack->modify_rect.h > 0) {
			dfb_region_from_rectangle( &region, &stack->modify_rect );
     		dfb_wm_arib_update_window( window, &region, 0 );
		}
	}
	dfb_windowstack_unlock( stack );

	return DFB_OK;
}

