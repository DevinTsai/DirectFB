/*
   (c) Copyright 2000-2002  convergence integrated media GmbH.
   (c) Copyright 2002-2004  convergence GmbH.

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
              Sven Neumann <neo@directfb.org> and
              Ville Syrj�l� <syrjala@sci.fi>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>

#include <math.h>


#include <directfb.h>

#include <core/core.h>
#include <core/coredefs.h>
#include <core/coretypes.h>

#include <core/gfxcard.h>
#include <core/fonts.h>
#include <core/state.h>
#include <core/palette.h>
#include <core/layers.h>
#include <core/layer_region.h>
#include <core/surfaces.h>
#include <core/surfacemanager.h>

#include <media/idirectfbfont.h>

#include <display/idirectfbsurface.h>
#include <display/idirectfbpalette.h>

#include <misc/util.h>

#include <direct/interface.h>
#include <direct/mem.h>
#include <direct/memcpy.h>
#include <direct/messages.h>
#include <direct/util.h>

#include <gfx/convert.h>
#include <gfx/util.h>


D_DEBUG_DOMAIN( Surface, "IDirectFBSurface", "IDirectFBSurface Interface" );


/**********************************************************************************************************************/

static ReactionResult IDirectFBSurface_listener( const void *msg_data, void *ctx );

/**********************************************************************************************************************/

void
IDirectFBSurface_Destruct( IDirectFBSurface *thiz )
{
     IDirectFBSurface_data *data;

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     D_ASSERT( thiz != NULL );

     data = thiz->priv;

     if(data->surface->layer_region)
        dfb_layer_region_unref( data->surface->layer_region );

     if (data->surface)
          dfb_surface_detach( data->surface, &data->reaction );

     dfb_state_set_destination( &data->state, NULL );
     dfb_state_set_source( &data->state, NULL );

     dfb_state_destroy( &data->state );

     if (data->font) {
          IDirectFBFont      *font      = data->font;
          IDirectFBFont_data *font_data = font->priv;

          if (font_data) {
               if (data->surface)
                    dfb_font_drop_destination( font_data->font, data->surface );

               font->Release( font );
          }
          else
               D_WARN( "font dead?" );
     }

     if (data->surface) {
          if (data->locked)
               dfb_surface_unlock( data->surface, data->locked - 1 );

          dfb_surface_unref( data->surface );
     }

     DIRECT_DEALLOCATE_INTERFACE( thiz );
}

static DFBResult
IDirectFBSurface_AddRef( IDirectFBSurface *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     data->ref++;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Release( IDirectFBSurface *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (--data->ref == 0)
          IDirectFBSurface_Destruct( thiz );

     return DFB_OK;
}


static DFBResult
IDirectFBSurface_GetPixelFormat( IDirectFBSurface      *thiz,
                                 DFBSurfacePixelFormat *format )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!format)
          return DFB_INVARG;

     *format = data->surface->format;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetAccelerationMask( IDirectFBSurface    *thiz,
                                      IDirectFBSurface    *source,
                                      DFBAccelerationMask *mask )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!mask)
          return DFB_INVARG;

     dfb_gfxcard_state_check( &data->state, DFXL_FILLRECTANGLE );
     dfb_gfxcard_state_check( &data->state, DFXL_DRAWRECTANGLE );
     dfb_gfxcard_state_check( &data->state, DFXL_DRAWLINE );
     dfb_gfxcard_state_check( &data->state, DFXL_FILLTRIANGLE );

     if (source) {
          IDirectFBSurface_data *src_data = source->priv;

          dfb_state_set_source( &data->state, src_data->surface );

          dfb_gfxcard_state_check( &data->state, DFXL_BLIT );
          dfb_gfxcard_state_check( &data->state, DFXL_STRETCHBLIT );
          dfb_gfxcard_state_check( &data->state, DFXL_TEXTRIANGLES );
     }

     if (data->font) {
          IDirectFBFont_data *font_data = data->font->priv;

          dfb_gfxcard_drawstring_check_state( font_data->font, &data->state );
     }

     *mask = data->state.accel;

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}
static DFBResult
IDirectFBSurface_GetPosition( IDirectFBSurface *thiz,
                          int              *x,
                          int              *y )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!x && !y)
          return DFB_INVARG;

     if (x)
          *x = data->x;

     if (y)
          *y = data->y;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetSize( IDirectFBSurface *thiz,
                          int              *width,
                          int              *height )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!width && !height)
          return DFB_INVARG;

     if (width)
          *width = data->area.wanted.w;

     if (height)
          *height = data->area.wanted.h;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetVisibleRectangle( IDirectFBSurface *thiz,
                                      DFBRectangle     *rect )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!rect)
          return DFB_INVARG;

     rect->x = data->area.current.x - data->area.wanted.x;
     rect->y = data->area.current.y - data->area.wanted.y;
     rect->w = data->area.current.w;
     rect->h = data->area.current.h;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetCapabilities( IDirectFBSurface       *thiz,
                                  DFBSurfaceCapabilities *caps )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!caps)
          return DFB_INVARG;

     *caps = data->caps;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetPalette( IDirectFBSurface  *thiz,
                             IDirectFBPalette **interface )
{
     DFBResult         ret;
     CoreSurface      *surface;
     IDirectFBPalette *palette;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (!surface->palette)
          return DFB_UNSUPPORTED;

     if (!interface)
          return DFB_INVARG;

     DIRECT_ALLOCATE_INTERFACE( palette, IDirectFBPalette );

     ret = IDirectFBPalette_Construct( palette, surface->palette );
     if (ret)
          return ret;

     *interface = palette;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetPalette( IDirectFBSurface *thiz,
                             IDirectFBPalette *palette )
{
     CoreSurface           *surface;
     IDirectFBPalette_data *palette_data;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (!palette)
          return DFB_INVARG;

     if (! DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          return DFB_UNSUPPORTED;

     palette_data = (IDirectFBPalette_data*) palette->priv;
     if (!palette_data)
          return DFB_DEAD;

     if (!palette_data->palette)
          return DFB_DESTROYED;

     dfb_surface_set_palette( surface, palette_data->palette );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetAlphaRamp( IDirectFBSurface *thiz,
                               __u8              a0,
                               __u8              a1,
                               __u8              a2,
                               __u8              a3 )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     dfb_surface_set_alpha_ramp( data->surface, a0, a1, a2, a3 );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Lock( IDirectFBSurface *thiz,
                       DFBSurfaceLockFlags flags,
                       void **ret_ptr, int *ret_pitch )
{
     DFBResult  ret;
     int        front;
     int        pitch;
     void      *ptr;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (data->locked)
          return DFB_LOCKED;

     if (!flags || !ret_ptr || !ret_pitch)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     front = (flags & DSLF_WRITE) ? 0 : 1;

     ret = dfb_surface_soft_lock( data->surface, flags, &ptr, &pitch, front );
     if (ret)
          return ret;

     ptr += pitch * data->area.current.y +
            DFB_BYTES_PER_LINE( data->surface->format, data->area.current.x );

     data->locked = front + 1;

     *ret_ptr   = ptr;
     *ret_pitch = pitch;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Unlock( IDirectFBSurface *thiz )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (data->locked)
          dfb_surface_unlock( data->surface, data->locked - 1 );

     data->locked = 0;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Flip( IDirectFBSurface    *thiz,
                       const DFBRegion     *region,
                       DFBSurfaceFlipFlags  flags )
{
     DFBRegion    reg;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p, %p, 0x%08x )\n", __FUNCTION__, thiz, region, flags );

     if (!data->surface)
          return DFB_DESTROYED;

     if (data->locked)
          return DFB_LOCKED;


     if (!data->compositeCallback && 
                     !(data->surface->caps & DSCAPS_FLIPPING) && 
                     (!(data->surface->caps & DSCAPS_TRIPLE) || 
                     !(data->surface->caps & DSCAPS_DOUBLE))
        ) { 
          printf(" FLIP NOT SUPPORTED !!! %p flipping=%d tr=%d d=%d total=%d\n",data->compositeCallback,(data->surface->caps & DSCAPS_FLIPPING),
                          (data->surface->caps & DSCAPS_TRIPLE),(data->surface->caps & DSCAPS_DOUBLE),
                 !(
                     !(data->surface->caps & DSCAPS_FLIPPING) && 
                     (!(data->surface->caps & DSCAPS_TRIPLE) || 
                     !(data->surface->caps & DSCAPS_DOUBLE))
                  )
                         );
          return DFB_UNSUPPORTED;
     }

     if (!data->area.current.w || !data->area.current.h ||
         (region && (region->x1 > region->x2 || region->y1 > region->y2)))
          return DFB_INVAREA;


     dfb_region_from_rectangle( &reg, &data->area.current );

     if (region) {
          DFBRegion clip = DFB_REGION_INIT_TRANSLATED( region,
                                                       data->area.wanted.x,
                                                       data->area.wanted.y );

          if (!dfb_region_region_intersect( &reg, &clip ))
               return DFB_INVAREA;
     }

     D_DEBUG_AT( Surface, "  -> %d, %d - %dx%d\n", DFB_RECTANGLE_VALS_FROM_REGION( &reg ) );

     //if no parent or buffered flip our surface
     //don't auto flip our parents if its shared and there is a composite
     if( !(flags & DSFLIP_COMPOSITE) && !data->surface->coreCompositeCallback ) {

        //flip myself
        if ((data->surface->caps & DSCAPS_FLIPPING) ){
             printf("REGULAR FLIPPING \n");
            if (!(flags & DSFLIP_BLIT) && reg.x1 == 0 && reg.y1 == 0 &&
                reg.x2 == data->surface->width - 1 && reg.y2 == data->surface->height - 1)
                dfb_surface_flip_buffers( data->surface, false );
            else
                dfb_back_to_front_copy( data->surface, &reg );
        }
        if( data->compositeCallback ) { 
            flags |= DSFLIP_COMPOSITE; 
            thiz->Flip(thiz,region,flags);
        }
        return;
     } 

     /* User is going to composite flipped surfaces together*/
     if( data->compositeCallback) {
             printf("COMPOSITE FLIPPING \n");
        data->compositeCallback(thiz,&reg,flags);
     }

     /* System wants to composite used by layers*/
     if ( data->surface->coreCompositeCallback ) {
             printf("CORE COMPOSITE FLIPPING \n");
        data->surface->coreCompositeCallback(data->surface,&reg,flags);
     }

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetField( IDirectFBSurface    *thiz,
                           int                  field )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "REGULAR%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!(data->surface->caps & DSCAPS_INTERLACED))
          return DFB_UNSUPPORTED;

     if (field < 0 || field > 1)
          return DFB_INVARG;

     dfb_surface_set_field( data->surface, field );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Clear( IDirectFBSurface *thiz,
                        __u8 r, __u8 g, __u8 b, __u8 a )
{
     DFBColor                old_color;
     unsigned int            old_index;
     DFBSurfaceDrawingFlags  old_flags;
     CoreSurface            *surface;
     DFBColor                color = { a, r, g, b };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     /* save current color and drawing flags */
     old_color = data->state.color;
     old_index = data->state.color_index;
     old_flags = data->state.drawingflags;

     /* set drawing flags */
     dfb_state_set_drawing_flags( &data->state, DSDRAW_NOFX );

     /* set color */
     if (DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          dfb_state_set_color_index( &data->state,
                                     dfb_palette_search( surface->palette, r, g, b, a ) );
     else
          dfb_state_set_color( &data->state, &color );

     /* fill the visible rectangle */
     dfb_gfxcard_fillrectangles( &data->area.current, 1, &data->state );

     /* clear the depth buffer */
     if (data->caps & DSCAPS_DEPTH)
          dfb_clear_depth( data->surface, &data->state.clip );

     /* restore drawing flags */
     dfb_state_set_drawing_flags( &data->state, old_flags );

     /* restore color */
     if (DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          dfb_state_set_color_index( &data->state, old_index );
     else
          dfb_state_set_color( &data->state, &old_color );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetClip( IDirectFBSurface *thiz, const DFBRegion *clip )
{
     DFBRegion newclip;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (clip) {
          newclip = DFB_REGION_INIT_TRANSLATED( clip, data->area.wanted.x, data->area.wanted.y );

          if (!dfb_unsafe_region_rectangle_intersect( &newclip,
                                                      &data->area.wanted ))
               return DFB_INVARG;

          data->clip_set = true;
          data->clip_wanted = newclip;

          if (!dfb_region_rectangle_intersect( &newclip, &data->area.current ))
               return DFB_INVAREA;
     }
     else {
          dfb_region_from_rectangle( &newclip, &data->area.current );
          data->clip_set = false;
     }

     dfb_state_set_clip( &data->state, &newclip );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetClip( IDirectFBSurface *thiz, DFBRegion *ret_clip )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!ret_clip)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     *ret_clip = DFB_REGION_INIT_TRANSLATED( &data->state.clip, -data->area.wanted.x, -data->area.wanted.y );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetColor( IDirectFBSurface *thiz,
                           __u8 r, __u8 g, __u8 b, __u8 a )
{
     CoreSurface *surface;
     DFBColor     color = { a, r, g, b };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     dfb_state_set_color( &data->state, &color );

     if (DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          dfb_state_set_color_index( &data->state,
                                     dfb_palette_search( surface->palette, r, g, b, a ) );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetColorIndex( IDirectFBSurface *thiz,
                                unsigned int      index )
{
     CoreSurface *surface;
     CorePalette *palette;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (! DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          return DFB_UNSUPPORTED;

     palette = surface->palette;
     if (!palette)
          return DFB_UNSUPPORTED;

     if (index > palette->num_entries)
          return DFB_INVARG;

     dfb_state_set_color( &data->state, &palette->entries[index] );

     dfb_state_set_color_index( &data->state, index );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetSrcBlendFunction( IDirectFBSurface        *thiz,
                                      DFBSurfaceBlendFunction  src )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     switch (src) {
          case DSBF_ZERO:
          case DSBF_ONE:
          case DSBF_SRCCOLOR:
          case DSBF_INVSRCCOLOR:
          case DSBF_SRCALPHA:
          case DSBF_INVSRCALPHA:
          case DSBF_DESTALPHA:
          case DSBF_INVDESTALPHA:
          case DSBF_DESTCOLOR:
          case DSBF_INVDESTCOLOR:
          case DSBF_SRCALPHASAT:
               dfb_state_set_src_blend( &data->state, src );
               return DFB_OK;
     }

     return DFB_INVARG;
}

static DFBResult
IDirectFBSurface_SetDstBlendFunction( IDirectFBSurface        *thiz,
                                      DFBSurfaceBlendFunction  dst )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     switch (dst) {
          case DSBF_ZERO:
          case DSBF_ONE:
          case DSBF_SRCCOLOR:
          case DSBF_INVSRCCOLOR:
          case DSBF_SRCALPHA:
          case DSBF_INVSRCALPHA:
          case DSBF_DESTALPHA:
          case DSBF_INVDESTALPHA:
          case DSBF_DESTCOLOR:
          case DSBF_INVDESTCOLOR:
          case DSBF_SRCALPHASAT:
               dfb_state_set_dst_blend( &data->state, dst );
               return DFB_OK;
     }

     return DFB_INVARG;
}

static DFBResult
IDirectFBSurface_SetPorterDuff( IDirectFBSurface         *thiz,
                                DFBSurfacePorterDuffRule  rule )
{
     DFBSurfaceBlendFunction src;
     DFBSurfaceBlendFunction dst;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );


     switch (rule) {
          case DSPD_NONE:
               src = DSBF_SRCALPHA;
               dst = DSBF_INVSRCALPHA;
               break;
          case DSPD_CLEAR:
               src = DSBF_ZERO;
               dst = DSBF_ZERO;
               break;
          case DSPD_SRC:
               src = DSBF_ONE;
               dst = DSBF_ZERO;
               break;
          case DSPD_SRC_OVER:
               src = DSBF_ONE;
               dst = DSBF_INVSRCALPHA;
               break;
          case DSPD_DST_OVER:
               src = DSBF_INVDESTALPHA;
               dst = DSBF_ONE;
               break;
          case DSPD_SRC_IN:
               src = DSBF_DESTALPHA;
               dst = DSBF_ZERO;
               break;
          case DSPD_DST_IN:
               src = DSBF_ZERO;
               dst = DSBF_SRCALPHA;
               break;
          case DSPD_SRC_OUT:
               src = DSBF_INVDESTALPHA;
               dst = DSBF_ZERO;
               break;
          case DSPD_DST_OUT:
               src = DSBF_ZERO;
               dst = DSBF_INVSRCALPHA;
               break;
          case DSPD_SRC_ATOP:
               src = DSBF_DESTALPHA;
               dst = DSBF_INVSRCALPHA;
               break;
          case DSPD_DST_ATOP:
               src = DSBF_INVDESTALPHA;
               dst = DSBF_SRCALPHA;
               break;
          case DSPD_ADD:
               src = DSBF_ONE;
               dst = DSBF_ONE;
               break;
          case DSPD_XOR:
               src = DSBF_INVDESTALPHA;
               dst = DSBF_INVSRCALPHA;
               break;
          default:
               return DFB_INVARG;
     }

     dfb_state_set_src_blend( &data->state, src );
     dfb_state_set_dst_blend( &data->state, dst );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetSrcColorKey( IDirectFBSurface *thiz,
                                 __u8              r,
                                 __u8              g,
                                 __u8              b )
{
     CoreSurface *surface;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     data->src_key.r = r;
     data->src_key.g = g;
     data->src_key.b = b;

     if (DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          data->src_key.value = dfb_palette_search( surface->palette,
                                                    r, g, b, 0x80 );
     else
          data->src_key.value = dfb_color_to_pixel( surface->format, r, g, b );

     /* The new key won't be applied to this surface's state.
        The key will be taken by the destination surface to apply it
        to its state when source color keying is used. */

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetSrcColorKeyIndex( IDirectFBSurface *thiz,
                                      unsigned int      index )
{
     CoreSurface *surface;
     CorePalette *palette;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (! DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          return DFB_UNSUPPORTED;

     palette = surface->palette;
     if (!palette)
          return DFB_UNSUPPORTED;

     if (index > palette->num_entries)
          return DFB_INVARG;

     data->src_key.r = palette->entries[index].r;
     data->src_key.g = palette->entries[index].g;
     data->src_key.b = palette->entries[index].b;

     data->src_key.value = index;

     /* The new key won't be applied to this surface's state.
        The key will be taken by the destination surface to apply it
        to its state when source color keying is used. */

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetDstColorKey( IDirectFBSurface *thiz,
                                 __u8              r,
                                 __u8              g,
                                 __u8              b )
{
     CoreSurface *surface;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     data->dst_key.r = r;
     data->dst_key.g = g;
     data->dst_key.b = b;

     if (DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          data->dst_key.value = dfb_palette_search( surface->palette,
                                                    r, g, b, 0x80 );
     else
          data->dst_key.value = dfb_color_to_pixel( surface->format, r, g, b );

     dfb_state_set_dst_colorkey( &data->state, data->dst_key.value );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetDstColorKeyIndex( IDirectFBSurface *thiz,
                                      unsigned int      index )
{
     CoreSurface *surface;
     CorePalette *palette;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     if (! DFB_PIXELFORMAT_IS_INDEXED( surface->format ))
          return DFB_UNSUPPORTED;

     palette = surface->palette;
     if (!palette)
          return DFB_UNSUPPORTED;

     if (index > palette->num_entries)
          return DFB_INVARG;

     data->dst_key.r = palette->entries[index].r;
     data->dst_key.g = palette->entries[index].g;
     data->dst_key.b = palette->entries[index].b;

     data->dst_key.value = index;

     dfb_state_set_dst_colorkey( &data->state, data->dst_key.value );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetFont( IDirectFBSurface *thiz,
                          IDirectFBFont    *font )
{
     DFBResult ret;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (data->font != font) {
         if (font) {
              IDirectFBFont_data *font_data;

              ret = font->AddRef( font );
              if (ret)
                   return ret;

              DIRECT_INTERFACE_GET_DATA_FROM( font, font_data, IDirectFBFont );

              data->encoding = font_data->encoding;
         }

         if (data->font) {
              IDirectFBFont_data *old_data;
              IDirectFBFont      *old = data->font;

              DIRECT_INTERFACE_GET_DATA_FROM( old, old_data, IDirectFBFont );

              dfb_font_drop_destination( old_data->font, data->surface );

              old->Release( old );
         }

         data->font = font;
     }

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetFont( IDirectFBSurface  *thiz,
                          IDirectFBFont    **ret_font )
{
     DFBResult      ret;
     IDirectFBFont *font;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!ret_font)
          return DFB_INVARG;

     font = data->font;
     if (!font) {
          *ret_font = NULL;
          return DFB_MISSINGFONT;
     }

     ret = font->AddRef( font );
     if (ret)
          return ret;

     *ret_font = font;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetDrawingFlags( IDirectFBSurface       *thiz,
                                  DFBSurfaceDrawingFlags  flags )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     dfb_state_set_drawing_flags( &data->state, flags );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_FillRectangle( IDirectFBSurface *thiz,
                                int x, int y, int w, int h )
{
     DFBRectangle rect = { x, y, w, h };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (w<=0 || h<=0)
          return DFB_INVARG;

     rect.x += data->area.wanted.x;
     rect.y += data->area.wanted.y;

     dfb_gfxcard_fillrectangles( &rect, 1, &data->state );

     return DFB_OK;
}


static DFBResult
IDirectFBSurface_DrawLine( IDirectFBSurface *thiz,
                           int x1, int y1, int x2, int y2 )
{
     DFBRegion line = { x1, y1, x2, y2 };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     line.x1 += data->area.wanted.x;
     line.x2 += data->area.wanted.x;
     line.y1 += data->area.wanted.y;
     line.y2 += data->area.wanted.y;

     if (x1 == x2 || y1 == y2) {
          DFBRectangle rect = { line.x1, line.y1, line.x2 - line.x1 + 1, line.y2 - line.y1 + 1 };

          dfb_gfxcard_fillrectangles( &rect, 1, &data->state );
     }
     else
          dfb_gfxcard_drawlines( &line, 1, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_DrawLines( IDirectFBSurface *thiz,
                            const DFBRegion  *lines,
                            unsigned int      num_lines )
{
     DFBRegion *local_lines = alloca(sizeof(DFBRegion) * num_lines);

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!lines || !num_lines)
          return DFB_INVARG;

     if (data->area.wanted.x || data->area.wanted.y) {
          unsigned int i;

          for (i=0; i<num_lines; i++) {
               local_lines[i].x1 = lines[i].x1 + data->area.wanted.x;
               local_lines[i].x2 = lines[i].x2 + data->area.wanted.x;
               local_lines[i].y1 = lines[i].y1 + data->area.wanted.y;
               local_lines[i].y2 = lines[i].y2 + data->area.wanted.y;
          }
     }
     else
          /* clipping may modify lines, so we copy them */
          direct_memcpy( local_lines, lines, sizeof(DFBRegion) * num_lines );

     dfb_gfxcard_drawlines( local_lines, num_lines, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_DrawRectangle( IDirectFBSurface *thiz,
                                int x, int y, int w, int h )
{
     DFBRectangle rect = { x, y, w, h };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (w<=0 || h<=0)
          return DFB_INVARG;

     rect.x += data->area.wanted.x;
     rect.y += data->area.wanted.y;

     dfb_gfxcard_drawrectangle( &rect, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_FillTriangle( IDirectFBSurface *thiz,
                               int x1, int y1,
                               int x2, int y2,
                               int x3, int y3 )
{
     DFBTriangle tri = { x1, y1, x2, y2, x3, y3 };

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     tri.x1 += data->area.wanted.x;
     tri.y1 += data->area.wanted.y;
     tri.x2 += data->area.wanted.x;
     tri.y2 += data->area.wanted.y;
     tri.x3 += data->area.wanted.x;
     tri.y3 += data->area.wanted.y;

     dfb_gfxcard_filltriangle( &tri, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_FillRectangles( IDirectFBSurface   *thiz,
                                 const DFBRectangle *rects,
                                 unsigned int        num_rects )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!rects || !num_rects)
          return DFB_INVARG;

     if (data->area.wanted.x || data->area.wanted.y) {
          unsigned int  i;
          DFBRectangle *local_rects;
          bool          malloced = (num_rects > 256);

          if (malloced)
               local_rects = malloc( sizeof(DFBRectangle) * num_rects );
          else
               local_rects = alloca( sizeof(DFBRectangle) * num_rects );

          for (i=0; i<num_rects; i++) {
               local_rects[i].x = rects[i].x + data->area.wanted.x;
               local_rects[i].y = rects[i].y + data->area.wanted.y;
               local_rects[i].w = rects[i].w;
               local_rects[i].h = rects[i].h;
          }

          dfb_gfxcard_fillrectangles( local_rects, num_rects, &data->state );

          if (malloced)
               free( local_rects );
     }
     else
          dfb_gfxcard_fillrectangles( rects, num_rects, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_FillSpans( IDirectFBSurface *thiz,
                            int               y,
                            const DFBSpan    *spans,
                            unsigned int      num_spans )
{
     DFBSpan *local_spans = alloca(sizeof(DFBSpan) * num_spans);

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!spans || !num_spans)
          return DFB_INVARG;

     if (data->area.wanted.x || data->area.wanted.y) {
          unsigned int i;

          for (i=0; i<num_spans; i++) {
               local_spans[i].x = spans[i].x + data->area.wanted.x;
               local_spans[i].w = spans[i].w;
          }
     }
     else
          /* clipping may modify spans, so we copy them */
          direct_memcpy( local_spans, spans, sizeof(DFBSpan) * num_spans );

     dfb_gfxcard_fillspans( y + data->area.wanted.y, local_spans, num_spans, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetBlittingFlags( IDirectFBSurface        *thiz,
                                   DFBSurfaceBlittingFlags  flags )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     dfb_state_set_blitting_flags( &data->state, flags );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_Blit( IDirectFBSurface   *thiz,
                       IDirectFBSurface   *source,
                       const DFBRectangle *sr,
                       int dx, int dy )
{
     DFBRectangle srect;
     IDirectFBSurface_data *src_data;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!source)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;


     src_data = (IDirectFBSurface_data*)source->priv;

     if (!src_data->area.current.w || !src_data->area.current.h)
          return DFB_INVAREA;


     if (sr) {
          if (sr->w < 1  ||  sr->h < 1)
               return DFB_OK;

          srect = *sr;

          srect.x += src_data->area.wanted.x;
          srect.y += src_data->area.wanted.y;

          if (!dfb_rectangle_intersect( &srect, &src_data->area.current ))
               return DFB_INVAREA;

          dx += srect.x - (sr->x + src_data->area.wanted.x);
          dy += srect.y - (sr->y + src_data->area.wanted.y);
     }
     else {
          srect = src_data->area.current;

          dx += srect.x - src_data->area.wanted.x;
          dy += srect.y - src_data->area.wanted.y;
     }

     dfb_state_set_source( &data->state, src_data->surface );

     /* fetch the source color key from the source if necessary */
     if (data->state.blittingflags & DSBLIT_SRC_COLORKEY)
          dfb_state_set_src_colorkey( &data->state, src_data->src_key.value );

     dfb_gfxcard_blit( &srect,
                       data->area.wanted.x + dx,
                       data->area.wanted.y + dy, &data->state );

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_TileBlit( IDirectFBSurface   *thiz,
                           IDirectFBSurface   *source,
                           const DFBRectangle *sr,
                           int dx, int dy )
{
     DFBRectangle srect;
     IDirectFBSurface_data *src_data;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!source)
          return DFB_INVARG;


     src_data = (IDirectFBSurface_data*)source->priv;

     if (!src_data->area.current.w || !src_data->area.current.h)
          return DFB_INVAREA;


     if (sr) {
          if (sr->w < 1  ||  sr->h < 1)
               return DFB_OK;

          srect = *sr;

          srect.x += src_data->area.wanted.x;
          srect.y += src_data->area.wanted.y;

          if (!dfb_rectangle_intersect( &srect, &src_data->area.current ))
               return DFB_INVAREA;

          dx += srect.x - (sr->x + src_data->area.wanted.x);
          dy += srect.y - (sr->y + src_data->area.wanted.y);
     }
     else {
          srect = src_data->area.current;

          dx += srect.x - src_data->area.wanted.x;
          dy += srect.y - src_data->area.wanted.y;
     }

     dfb_state_set_source( &data->state, src_data->surface );

     /* fetch the source color key from the source if necessary */
     if (data->state.blittingflags & DSBLIT_SRC_COLORKEY)
          dfb_state_set_src_colorkey( &data->state, src_data->src_key.value );

     dx %= srect.w;
     if (dx > 0)
          dx -= srect.w;

     dy %= srect.h;
     if (dy > 0)
          dy -= srect.h;

     dx += data->area.wanted.x;
     dy += data->area.wanted.y;

     dfb_gfxcard_tileblit( &srect, dx, dy,
                           dx + data->area.wanted.w + srect.w - 1,
                           dy + data->area.wanted.h + srect.h - 1, &data->state );

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_BatchBlit( IDirectFBSurface   *thiz,
                            IDirectFBSurface   *source,
                            const DFBRectangle *source_rects,
                            const DFBPoint     *dest_points,
                            int                 num )
{
     int                    i, dx, dy, sx, sy;
     DFBRectangle          *rects;
     DFBPoint              *points;
     IDirectFBSurface_data *src_data;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!source || !source_rects || !dest_points || num < 1)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;


     src_data = (IDirectFBSurface_data*)source->priv;

     if (!src_data->area.current.w || !src_data->area.current.h)
          return DFB_INVAREA;

     dx = data->area.wanted.x;
     dy = data->area.wanted.y;

     sx = src_data->area.wanted.x;
     sy = src_data->area.wanted.y;

     rects  = alloca( sizeof(DFBRectangle) * num );
     points = alloca( sizeof(DFBPoint) * num );

     direct_memcpy( rects, source_rects, sizeof(DFBRectangle) * num );
     direct_memcpy( points, dest_points, sizeof(DFBPoint) * num );

     for (i=0; i<num; i++) {
          rects[i].x += sx;
          rects[i].y += sy;

          points[i].x += dx;
          points[i].y += dy;

          if (!dfb_rectangle_intersect( &rects[i], &src_data->area.current ))
               rects[i].w = rects[i].h = 0;

          points[i].x += rects[i].x - (source_rects[i].x + sx);
          points[i].y += rects[i].y - (source_rects[i].y + sy);
     }

     dfb_state_set_source( &data->state, src_data->surface );

     /* fetch the source color key from the source if necessary */
     if (data->state.blittingflags & DSBLIT_SRC_COLORKEY)
          dfb_state_set_src_colorkey( &data->state, src_data->src_key.value );

     dfb_gfxcard_batchblit( rects, points, num, &data->state );

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_StretchBlit( IDirectFBSurface   *thiz,
                              IDirectFBSurface   *source,
                              const DFBRectangle *source_rect,
                              const DFBRectangle *destination_rect )
{
     DFBRectangle srect, drect;
     IDirectFBSurface_data *src_data;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!source)
          return DFB_INVARG;


     src_data = (IDirectFBSurface_data*)source->priv;

     if (!src_data->area.current.w || !src_data->area.current.h)
          return DFB_INVAREA;


     /* do destination rectangle */
     if (destination_rect) {
          if (destination_rect->w < 1  ||  destination_rect->h < 1)
               return DFB_INVARG;

          drect = *destination_rect;

          drect.x += data->area.wanted.x;
          drect.y += data->area.wanted.y;
     }
     else
          drect = data->area.wanted;

     /* do source rectangle */
     if (source_rect) {
          if (source_rect->w < 1  ||  source_rect->h < 1)
               return DFB_INVARG;

          srect = *source_rect;

          srect.x += src_data->area.wanted.x;
          srect.y += src_data->area.wanted.y;
     }
     else
          srect = src_data->area.wanted;


     /* clipping of the source rectangle must be applied to the destination */
     {
          DFBRectangle orig_src = srect;

          if (!dfb_rectangle_intersect( &srect, &src_data->area.current ))
               return DFB_INVAREA;

          if (srect.x != orig_src.x)
               drect.x += (int)( (srect.x - orig_src.x) *
                                 (drect.w / (float)orig_src.w) + 0.5f);

          if (srect.y != orig_src.y)
               drect.y += (int)( (srect.y - orig_src.y) *
                                 (drect.h / (float)orig_src.h) + 0.5f);

          if (srect.w != orig_src.w)
               drect.w = D_ICEIL(drect.w * (srect.w / (float)orig_src.w));
          if (srect.h != orig_src.h)
               drect.h = D_ICEIL(drect.h * (srect.h / (float)orig_src.h));
     }

     dfb_state_set_source( &data->state, src_data->surface );

     /* fetch the source color key from the source if necessary */
     if (data->state.blittingflags & DSBLIT_SRC_COLORKEY)
          dfb_state_set_src_colorkey( &data->state, src_data->src_key.value );

     dfb_gfxcard_stretchblit( &srect, &drect, &data->state );

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}

#define SET_VERTEX(v,X,Y,Z,W,S,T)  \
     do {                          \
          (v)->x = X;              \
          (v)->y = Y;              \
          (v)->z = Z;              \
          (v)->w = W;              \
          (v)->s = S;              \
          (v)->t = T;              \
     } while (0)

static DFBResult
IDirectFBSurface_TextureTriangles( IDirectFBSurface     *thiz,
                                   IDirectFBSurface     *source,
                                   const DFBVertex      *vertices,
                                   const int            *indices,
                                   int                   num,
                                   DFBTriangleFormation  formation )
{
     int                    i;
     DFBVertex             *translated;
     IDirectFBSurface_data *src_data;
     bool                   src_sub;
     float                  x0 = 0;
     float                  y0 = 0;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;


     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!source || !vertices || num < 3)
          return DFB_INVARG;

     src_data = (IDirectFBSurface_data*)source->priv;

     if ((src_sub = (src_data->caps & DSCAPS_SUBSURFACE))) {
          D_ONCE( "sub surface texture not fully working with 'repeated' mapping" );

          x0 = data->area.wanted.x;
          y0 = data->area.wanted.y;
     }

     switch (formation) {
          case DTTF_LIST:
               if (num % 3)
                    return DFB_INVARG;
               break;

          case DTTF_STRIP:
          case DTTF_FAN:
               break;

          default:
               return DFB_INVARG;
     }

     translated = alloca( num * sizeof(DFBVertex) );
     if (!translated)
          return DFB_NOSYSTEMMEMORY;

     /* TODO: pass indices through to driver */
     if (src_sub) {
          float oowidth  = 1.0f / src_data->surface->width;
          float ooheight = 1.0f / src_data->surface->height;

          float s0 = src_data->area.wanted.x * oowidth;
          float t0 = src_data->area.wanted.y * ooheight;

          float fs = src_data->area.wanted.w * oowidth;
          float ft = src_data->area.wanted.h * ooheight;

          for (i=0; i<num; i++) {
               const DFBVertex *in  = &vertices[ indices ? indices[i] : i ];
               DFBVertex       *out = &translated[i];

               SET_VERTEX( out, x0 + in->x, y0 + in->y, in->z, in->w,
                           s0 + fs * in->s, t0 + ft * in->t );
          }
     }
     else {
          if (indices) {
               for (i=0; i<num; i++) {
                    const DFBVertex *in  = &vertices[ indices[i] ];
                    DFBVertex       *out = &translated[i];

                    SET_VERTEX( out, x0 + in->x, y0 + in->y, in->z, in->w, in->s, in->t );
               }
          }
          else {
               direct_memcpy( translated, vertices, num * sizeof(DFBVertex) );

               for (i=0; i<num; i++) {
                    translated[i].x += x0;
                    translated[i].y += y0;
               }
          }
     }

     dfb_state_set_source( &data->state, src_data->surface );

     /* fetch the source color key from the source if necessary */
     if (data->state.blittingflags & DSBLIT_SRC_COLORKEY)
          dfb_state_set_src_colorkey( &data->state, src_data->src_key.value );

     dfb_gfxcard_texture_triangles( translated, num, formation, &data->state );

     /* unref the source surface so it can be deleted immediately */
     dfb_state_set_source( &data->state, NULL );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_DrawString( IDirectFBSurface *thiz,
                             const char *text, int bytes,
                             int x, int y,
                             DFBSurfaceTextFlags flags )
{
     DFBResult           ret;
     IDirectFBFont      *font;
     IDirectFBFont_data *font_data;
     CoreFont           *core_font;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!text)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!data->font)
          return DFB_MISSINGFONT;

     if (bytes < 0)
          bytes = strlen (text);

     if (bytes == 0)
          return DFB_OK;

     font = data->font;

     DIRECT_INTERFACE_GET_DATA_FROM( font, font_data, IDirectFBFont );

     core_font = font_data->font;

     if (!(flags & DSTF_TOP)) {
          y -= core_font->ascender;

          if (flags & DSTF_BOTTOM)
               y += core_font->descender;
     }

     if (flags & (DSTF_RIGHT | DSTF_CENTER)) {
          int          i, num, kx;
          int          width = 0;
          unsigned int prev = 0;
          unsigned int indices[bytes];

          /* FIXME: Avoid double locking and decoding. */
          dfb_font_lock( core_font );

          /* Decode string to character indices. */
          ret = dfb_font_decode_text( core_font, data->encoding, text, bytes, indices, &num );
          if (ret) {
               dfb_font_unlock( core_font );
               return ret;
          }

          /* Calculate string width. */
          for (i=0; i<num; i++) {
               unsigned int   current = indices[i];
               CoreGlyphData *glyph;

               if (dfb_font_get_glyph_data( core_font, current, &glyph ) == DFB_OK) {
                    width += glyph->advance;

                    if (prev && core_font->GetKerning &&
                        core_font->GetKerning( core_font, prev, current, &kx, NULL ) == DFB_OK)
                         width += kx;
               }

               prev = current;
          }

          dfb_font_unlock( core_font );

          /* Justify. */
          if (flags & DSTF_RIGHT)
               x -= width;
          else if (flags & DSTF_CENTER)
               x -= width >> 1;
     }

     dfb_gfxcard_drawstring( (const unsigned char*) text, bytes, data->encoding,
                             data->area.wanted.x + x, data->area.wanted.y + y,
                             core_font, &data->state );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_DrawGlyph( IDirectFBSurface *thiz,
                            unsigned int character, int x, int y,
                            DFBSurfaceTextFlags flags )
{
     DFBResult           ret;
     IDirectFBFont      *font;
     IDirectFBFont_data *font_data;
     CoreFont           *core_font;
     unsigned int        index;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p, 0x%x, %d,%d, 0x%x )\n",
                 __FUNCTION__, thiz, character, x, y, flags );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!character)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->locked)
          return DFB_LOCKED;

     if (!data->font)
          return DFB_MISSINGFONT;

     font = data->font;

     DIRECT_INTERFACE_GET_DATA_FROM( font, font_data, IDirectFBFont );

     core_font = font_data->font;

     /* FIXME: Avoid double locking. */
     dfb_font_lock( core_font );

     ret = dfb_font_decode_character( core_font, data->encoding, character, &index );
     if (ret) {
          dfb_font_unlock( core_font );
          return ret;
     }

     if (!(flags & DSTF_TOP)) {
          y -= core_font->ascender;

          if (flags & DSTF_BOTTOM)
               y += core_font->descender;
     }

     if (flags & (DSTF_RIGHT | DSTF_CENTER)) {
          CoreGlyphData *glyph;

          ret = dfb_font_get_glyph_data( core_font, index, &glyph );
          if (ret) {
               dfb_font_unlock( core_font );
               return ret;
          }

          if (flags & DSTF_RIGHT)
               x -= glyph->advance;
          else if (flags & DSTF_CENTER)
               x -= glyph->advance >> 1;
     }

     dfb_gfxcard_drawglyph( index,
                            data->area.wanted.x + x, data->area.wanted.y + y,
                            core_font, &data->state );

     dfb_font_unlock( core_font );

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetEncoding( IDirectFBSurface  *thiz,
                              DFBTextEncodingID  encoding )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p, %d )\n", __FUNCTION__, thiz, encoding );

     /* TODO: check for support or fail later? */
     data->encoding = encoding;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetSubSurface( IDirectFBSurface    *thiz,
                                const DFBRectangle  *rect,
                                IDirectFBSurface   **surface )
{
     DFBResult ret;
     IDirectFBSurface *isurface;
     DFBRectangle wanted, granted;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     /* Check arguments */
     if (!data->surface)
          return DFB_DESTROYED;

     if (!surface)
          return DFB_INVARG;

          
     /* Allocate interface */
     DIRECT_ALLOCATE_INTERFACE( *surface, IDirectFBSurface );

      /*reference the layer region if were tied to a layer*/
#if 0
      if( data->surface->layer_region ) {
             if (dfb_layer_region_ref( data->surface->layer_region ))
                 return DFB_FUSION;
      }  
#endif
 
     if (rect || data->limit_set) {
          
          /* Compute wanted rectangle */
          if (rect) {
               wanted = *rect;

               wanted.x += data->area.wanted.x;
               wanted.y += data->area.wanted.y;

               if (wanted.w <= 0 || wanted.h <= 0) {
                    wanted.w = 0;
                    wanted.h = 0;
               }
          }
          else {
               wanted = data->area.wanted;
          }
          
          /* Compute granted rectangle */
          granted = wanted;

          dfb_rectangle_intersect( &granted, &data->area.granted );
          
     }else {
         wanted.w = data->surface->width;
         wanted.h = data->surface->height;
         granted = wanted;
     }
     /* Construct */
     ret = IDirectFBSurface_Construct( *surface, 
                                            &wanted, &granted, &data->area.insets,
                                            /*data->surface*/NULL,
                                            data->caps | DSCAPS_SUBSURFACE );
     
     if( ret )
          return ret;
  
     /*set up the interface chains*/     
     {
          IDirectFBSurface_data *child_data = 
               (IDirectFBSurface_data*)(*surface)->priv;

          child_data->x = wanted.x;
          child_data->y = wanted.y;

          child_data->parent = thiz;
          child_data->surface = NULL;

          if(!data->first_child)
               data->first_child = *surface; 
          child_data->prev_sibling = data->last_child;

          if( data->last_child) {
               IDirectFBSurface_data *last_data = 
                       (IDirectFBSurface_data*)(data->last_child)->priv;
               last_data->next_sibling = *surface;
          }
          data->last_child = *surface; 
          /*Copy parents local interface composite  callback*/
          child_data->compositeCallback = data->compositeCallback; 
     }

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_GetGL( IDirectFBSurface   *thiz,
                        IDirectFBGL       **interface )
{
     DFBResult ret;
     DirectInterfaceFuncs *funcs = NULL;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!data->surface)
          return DFB_DESTROYED;

     if (!interface)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;


     ret = DirectGetInterface( &funcs, "IDirectFBGL", NULL, NULL, NULL );
     if (ret)
          return ret;

     ret = funcs->Allocate( (void**)interface );
     if (ret)
          return ret;

     ret = funcs->Construct( *interface, thiz );
     if (ret)
          *interface = NULL;

     return ret;
}

static DFBResult
IDirectFBSurface_Dump( IDirectFBSurface   *thiz,
                       const char         *directory,
                       const char         *prefix )
{
     CoreSurface *surface;

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (!directory || !prefix)
          return DFB_INVARG;

     if (!data->area.current.w || !data->area.current.h)
          return DFB_INVAREA;

     if (data->caps & DSCAPS_SUBSURFACE) {
          D_ONCE( "sub surface dumping not supported yet" );
          return DFB_UNSUPPORTED;
     }

     surface = data->surface;
     if (!surface)
          return DFB_DESTROYED;

     return dfb_surface_dump( surface, directory, prefix );
}

static DFBResult
IDirectFBSurface_DisableAcceleration( IDirectFBSurface    *thiz,
                                      DFBAccelerationMask  mask )
{
     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     if (D_FLAGS_INVALID( mask, DFXL_ALL ))
          return DFB_INVARG;

     data->state.disabled = mask;

     return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetCompositeCallback( IDirectFBSurface    *thiz,
                                      CompositeCallback  callback )
{
  DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)
  D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );
  data->compositeCallback = callback;
  return DFB_OK;
}

static DFBResult
IDirectFBSurface_SetDescriptionCallback( IDirectFBSurface    *thiz,
                                      DescriptionCallback  callback )
{
  DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)
  D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );
  data->descriptionCallback = callback;
  return DFB_OK;
}

/*
 * Reconfigure a surface
 */
static DFBResult
IDirectFBSurface_SetDescription( IDirectFBSurface    *thiz,
                                      DFBSurfaceDescription *desc)
{
#define PRIV(interface) \
     interface?(IDirectFBSurface_data*)interface->priv:NULL 

     DIRECT_INTERFACE_GET_DATA(IDirectFBSurface)
     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );
     bool buffered;
     bool resized;
     bool unref_layer;
     bool unref_surface;
     DFBRectangle rect;
     IDirectFBSurface_data old_data;
     CoreSurface *surface;
     CoreLayerRegion *new_layer;
     DFBResult ret;

     if (!desc)
          return DFB_INVARG;

     IDirectFBSurface_data *parent_data = PRIV(data->parent);

     old_data = *data;


     /*First handle a move to a sibling with a different parent*/
     if( (desc->flags & DSDESC_PREV_SIBLING) || (desc->flags & DSDESC_NEXT_SIBLING) ) {
          if( desc->flags & DSDESC_PREV_SIBLING ) {
               IDirectFBSurface_data *prev = PRIV(desc->prev_sibling);
               if(prev && prev->parent != data->parent){
                  desc->flags |= DSDESC_PARENT;
                  desc->parent = prev->parent;
               }
          }
          else if ( desc->flags & DSDESC_NEXT_SIBLING ) {
               IDirectFBSurface_data *next = PRIV(desc->next_sibling);
               if(next && next->parent != data->parent){
                  desc->flags |= DSDESC_PARENT;
                  desc->parent = next->parent;
               }
          }
     }

     /*Next handle reparenting*/
     if (desc->flags & DSDESC_PARENT && data->parent != desc->parent) {
          /*remove*/
          IDirectFBSurface_data *new_parent_data = PRIV(desc->parent);
          IDirectFBSurface_data *prev = PRIV(data->prev_sibling);
          IDirectFBSurface_data *next = PRIV(data->next_sibling);
          {
          /*Redo the layer binding maybe moved offscreen or to a new screen*/
               new_layer = parent_data ?parent_data->surface->layer_region:NULL;
               //if( data->surface->layer_region && 
                //               data->surface->layer_region != new_layer )
                    //unref_layer = true;
          }
          /*fixup old parent and remove*/
          if( parent_data ) {
               if( parent_data->first_child == thiz )
                    parent_data->first_child = data->next_sibling;
               if( parent_data->last_child == thiz )
                    parent_data->last_child = data->prev_sibling;
               if( next )
                    next->prev_sibling = data->prev_sibling;
               if( prev )
                   prev->next_sibling = data->next_sibling;
               data->prev_sibling = NULL;
               data->next_sibling = NULL;
          }

          /*Now add to new parent */
          parent_data = new_parent_data;
          data->parent = desc->parent;

          /* callback is inherited from parent unless set*/
          data->compositeCallback = parent_data ? parent_data->compositeCallback:NULL; 
          data->descriptionCallback = parent_data ? parent_data->descriptionCallback:NULL; 

          /*add at end*/
          if( data->parent) {
                  
               if( !parent_data->first_child)
                    parent_data->first_child = thiz;

               if( parent_data->last_child  ) {
                    IDirectFBSurface_data *prev = PRIV(new_parent_data->last_child);
                    prev->next_sibling = thiz;
                    data->prev_sibling = new_parent_data->last_child;
               }
               parent_data->last_child = thiz;
          }
     }
     /*end parent change*/

     if( !data->parent && 
               ((desc->flags & DSDESC_PREV_SIBLING) || (desc->flags & DSDESC_NEXT_SIBLING)) ) {
          return DFB_INVARG;
     }

     /*fix up siblings */
     if( ((desc->flags & DSDESC_PREV_SIBLING) || (desc->flags & DSDESC_NEXT_SIBLING)) ) {
     
               IDirectFBSurface *prev_sibling = NULL;
               IDirectFBSurface_data *prev_sibling_data;
               IDirectFBSurface *next_sibling = NULL;
               IDirectFBSurface_data *next_sibling_data;
               IDirectFBSurface_data *prev = PRIV(data->prev_sibling);
               IDirectFBSurface_data *next = PRIV(data->next_sibling);
               data->parent = desc->parent;

               if(!parent_data->first_child )
                    return DFB_INVARG;

               if(desc->next_sibling)
                   next = PRIV(desc->next_sibling);

               
               if( desc->flags &  DSDESC_PREV_SIBLING ) {
                    prev_sibling = desc->prev_sibling;
                    prev = PRIV(prev_sibling);
                    if(prev)
                         next_sibling = prev->next_sibling;
                    if( prev_sibling && prev->parent != data->parent ) 
                         return DFB_INVARG;
               }
               else 
                 prev_sibling = NULL;


               if( desc->flags &  DSDESC_NEXT_SIBLING ) {
                    next_sibling = desc->next_sibling;
                    next = PRIV(next_sibling);
                    if(next)
                         prev_sibling = next->prev_sibling;
                    if( next_sibling && next->parent != data->parent ) 
                         return DFB_INVARG;
               }
               else
                 next_sibling == NULL;
                                  
               /*if they send both ?? */
               if( desc->flags & DSDESC_PREV_SIBLING && desc->flags & DSDESC_NEXT_SIBLING ) {
                     if( prev && prev->next_sibling != desc->prev_sibling )
                         return DFB_INVARG;
                     if( next && next->prev_sibling != desc->next_sibling )
                         return DFB_INVARG;
               }

               if( !(desc->flags & DSDESC_PREV_SIBLING) && !(desc->flags & DSDESC_NEXT_SIBLING) ) {
                    prev_sibling = parent_data->last_child;
                    prev = PRIV(prev_sibling);
               }

               /*remove myself from the chain */
               if(parent_data->first_child == thiz )
                    parent_data->first_child = data->next_sibling;
               if(parent_data->last_child == thiz )
                    parent_data->last_child = data->prev_sibling;
               if( prev )
                    prev->next_sibling = data->next_sibling;
               if( next ) 
                    next->prev_sibling = data->prev_sibling;

              /*redo its next sibling*/
              if( prev_sibling ) {
                    if( parent_data->last_child == prev_sibling )
                         parent_data->last_child = thiz;
                    data->prev_sibling = prev_sibling;
                    IDirectFBSurface_data *ppriv = PRIV(prev_sibling);
                    IDirectFBSurface_data *pnext = PRIV(ppriv->next_sibling);
                    data->next_sibling = ppriv->next_sibling;
                    pnext->prev_sibling = thiz;
              }

              if( next_sibling ) {
                    /*link in sibling chain first with next*/
                    if(parent_data->first_child == next_sibling )
                         parent_data->first_child = thiz;
                    data->next_sibling = next_sibling;
                    /*redo its next sibling*/
                    IDirectFBSurface_data *npriv = PRIV(next_sibling);
                    IDirectFBSurface_data *nprev = PRIV(npriv->prev_sibling);
                    data->prev_sibling = npriv->prev_sibling;
                    /* insert into previous*/
                    nprev->next_sibling = thiz;
                   /*make thiz prevous for next */
                    npriv->prev_sibling = thiz;
              }
      }

     /* Setup composite before any surface changes*/
     if ( desc->flags & DSDESC_COMPOSITE)
          data->compositeCallback = desc->compositeCallback;

     /* Setup description before any surface changes*/
     if ( desc->flags & DSDESC_DESCRIPTION)
          data->descriptionCallback = desc->descriptionCallback;

    /*currently buffered ?*/
    if(data->surface  &&  ( parent_data && data->surface != parent_data->surface) )
          buffered = true;
     /*
      * Buffering changed subsurface for now don't try to see if we already match
      */
#if 0
     if ( desc->flags & DSDESC_CAPS ) {
        /*already buffered ?*/
        if(data->surface && buffered)  
          dfb_surface_unref(data->surface);
     }
#endif

     rect = data->area.wanted;

     if(desc->flags & DSDESC_X )
        rect.x = desc->x;
     if(desc->flags & DSDESC_Y )
        rect.y = desc->y;
     if( desc->flags & DSDESC_WIDTH )
        rect.w = desc->width;
     if(desc->flags & DSDESC_HEIGHT )
        rect.h = desc->height;
    
    if(rect.w != data->area.wanted.w || rect.h != data->area.wanted.h)  
       resized = true;


    


     if ( (desc->flags & DSDESC_CAPS) && 
            (desc->caps & DSCAPS_TRIPLE ||desc->caps & DSCAPS_DOUBLE ) ) {
        int policy = CSP_VIDEOLOW;

        if (desc->caps & DSCAPS_VIDEOONLY)
            policy = CSP_VIDEOONLY;
        else if (desc->caps & DSCAPS_SYSTEMONLY)
           policy = CSP_SYSTEMONLY;

        //rect.x=rect.y=0;
        ret=dfb_surface_create(dfb_core_get(),
                              rect.w,rect.h,
                              parent_data->surface->format,
                              policy,
                              desc->caps,parent_data->surface->palette,
                              &surface);
        if(ret)
            return ret;
        rect.x=rect.y=0;
        
        ret = IDirectFBSurface_Construct(thiz, 
                                            &rect,&rect,&data->area.insets,
                                            surface,data->caps|DSCAPS_SUBSURFACE );
         data->surface = surface;
         data->area.wanted = data->area.granted = data->area.current = rect;

#if 0 
        /*Set up the core surface chains this should really be in create*/

        dfb_surfacemanager_lock( data->surface->manager );
        {
            data->surface->parent = data->surface;
            if(!data->surface->parent->first_child)
                data->surface->parent->first_child = data->surface; 
            data->surface->prev_sibling = data->surface->last_child;

            if( data->surface->last_child)
                data->surface->parent->last_child->next_sibling = data->surface;
            data->surface->parent->last_child = data->surface; 
            /* copy Parents core surface composite callback*/
            //data->surface->coreCompositeCallback = data->surface->parent->coreCompositeCallback; 
        }
#endif
        dfb_surface_notify_listeners( data->surface, CSNF_SIZEFORMAT );
        dfb_surfacemanager_unlock( surface->manager );
        return DFB_OK;
        
     } /* Makes a simple unbuffered */ 
     else if (desc->flags & DSDESC_CAPS) {
         if( parent_data )
               data->surface = parent_data->surface;
         else
           D_DERROR(0, "IDirectFBSurface: Unknow surface type \n" );
     }

     /*
      * Buffered resize
      */
      if( buffered ) {
          if(resized )  
               dfb_surface_reformat( dfb_core_get(), data->surface, rect.w, rect.h,surface->format);
          data->area.wanted = data->area.granted = data->area.current = rect;
      } /* Simple surface resize */
      else {
          DFBRectangle wanted, granted;
          /* Compute wanted rectangle */
          wanted = rect;
          wanted.x += parent_data->area.wanted.x;
          wanted.y += parent_data->area.wanted.y;
          if (wanted.w <= 0 || wanted.h <= 0) {
               wanted.w = 0;
               wanted.h = 0;
          }
        
          /* Compute granted rectangle */
          granted = wanted;
          dfb_rectangle_intersect( &granted, &parent_data->area.granted );
          data->area.wanted = wanted; 
          data->area.granted = data->area.current = granted;
     }
     if( old_data.surface->layer_region && 
         old_data.surface->layer_region != new_layer )
          dfb_layer_region_unref( old_data.surface->layer_region );
    /*buffering changed ??*/
    if(old_data.surface  &&  
         !( parent_data && old_data.surface == parent_data->surface) )
          dfb_surface_unref(old_data.surface);
     /*
      * Notify config listener that data changed
      */
     //data->descriptionCallback(thiz,&old_data,data);
#undef PRIV
}

/******/

DFBResult IDirectFBSurface_Construct( IDirectFBSurface       *thiz,
                                      DFBRectangle           *wanted,
                                      DFBRectangle           *granted,
                                      DFBInsets              *insets,
                                      CoreSurface            *surface,
                                      DFBSurfaceCapabilities  caps )
{
     DFBRectangle rect = { 0, 0, wanted->w,wanted->h };

     DIRECT_ALLOCATE_INTERFACE_DATA(thiz,IDirectFBSurface)

     D_DEBUG_AT( Surface, "%s( %p )\n", __FUNCTION__, thiz );

     data->ref = 1;
     data->caps = caps;
     if( surface )
          data->caps |= surface->caps;

     if ( surface  && dfb_surface_ref( surface )) {
          DIRECT_DEALLOCATE_INTERFACE(thiz);
          return DFB_FAILURE;
     }
     
     /* The area insets */
     if (insets) {
          data->area.insets = *insets;
          dfb_rectangle_subtract( &rect, insets );
     }

     /* The area that was requested */
     if (wanted)
          data->area.wanted = *wanted;
     else
          data->area.wanted = rect;

     /* The area that will never be exceeded */
     if (granted)
          data->area.granted = *granted;
     else
          data->area.granted = data->area.wanted;

     /* The currently accessible rectangle */
     data->area.current = data->area.granted;
     dfb_rectangle_intersect( &data->area.current, &rect );
     
     /* Whether granted rectangle is meaningful */
     data->limit_set = (granted != NULL);

     data->surface = surface;

     dfb_state_init( &data->state, NULL );
     dfb_state_set_destination( &data->state, surface );

     data->state.clip.x1  = data->area.current.x;
     data->state.clip.y1  = data->area.current.y;
     data->state.clip.x2  = data->area.current.x + (data->area.current.w ? : 1) - 1;
     data->state.clip.y2  = data->area.current.y + (data->area.current.h ? : 1) - 1;

     data->state.modified = SMF_ALL;

     thiz->AddRef = IDirectFBSurface_AddRef;
     thiz->Release = IDirectFBSurface_Release;

     thiz->GetCapabilities = IDirectFBSurface_GetCapabilities;
     thiz->GetPosition = IDirectFBSurface_GetPosition;
     thiz->GetSize = IDirectFBSurface_GetSize;
     thiz->GetVisibleRectangle = IDirectFBSurface_GetVisibleRectangle;
     thiz->GetPixelFormat = IDirectFBSurface_GetPixelFormat;
     thiz->GetAccelerationMask = IDirectFBSurface_GetAccelerationMask;

     thiz->GetPalette = IDirectFBSurface_GetPalette;
     thiz->SetPalette = IDirectFBSurface_SetPalette;
     thiz->SetAlphaRamp = IDirectFBSurface_SetAlphaRamp;

     thiz->Lock = IDirectFBSurface_Lock;
     thiz->Unlock = IDirectFBSurface_Unlock;
     thiz->Flip = IDirectFBSurface_Flip;
     thiz->SetField = IDirectFBSurface_SetField;
     thiz->Clear = IDirectFBSurface_Clear;

     thiz->SetClip = IDirectFBSurface_SetClip;
     thiz->GetClip = IDirectFBSurface_GetClip;
     thiz->SetColor = IDirectFBSurface_SetColor;
     thiz->SetColorIndex = IDirectFBSurface_SetColorIndex;
     thiz->SetSrcBlendFunction = IDirectFBSurface_SetSrcBlendFunction;
     thiz->SetDstBlendFunction = IDirectFBSurface_SetDstBlendFunction;
     thiz->SetPorterDuff = IDirectFBSurface_SetPorterDuff;
     thiz->SetSrcColorKey = IDirectFBSurface_SetSrcColorKey;
     thiz->SetSrcColorKeyIndex = IDirectFBSurface_SetSrcColorKeyIndex;
     thiz->SetDstColorKey = IDirectFBSurface_SetDstColorKey;
     thiz->SetDstColorKeyIndex = IDirectFBSurface_SetDstColorKeyIndex;

     thiz->SetBlittingFlags = IDirectFBSurface_SetBlittingFlags;
     thiz->Blit = IDirectFBSurface_Blit;
     thiz->TileBlit = IDirectFBSurface_TileBlit;
     thiz->BatchBlit = IDirectFBSurface_BatchBlit;
     thiz->StretchBlit = IDirectFBSurface_StretchBlit;
     thiz->TextureTriangles = IDirectFBSurface_TextureTriangles;

     thiz->SetDrawingFlags = IDirectFBSurface_SetDrawingFlags;
     thiz->FillRectangle = IDirectFBSurface_FillRectangle;
     thiz->DrawLine = IDirectFBSurface_DrawLine;
     thiz->DrawLines = IDirectFBSurface_DrawLines;
     thiz->DrawRectangle = IDirectFBSurface_DrawRectangle;
     thiz->FillTriangle = IDirectFBSurface_FillTriangle;
     thiz->FillRectangles = IDirectFBSurface_FillRectangles;
     thiz->FillSpans = IDirectFBSurface_FillSpans;

     thiz->SetFont = IDirectFBSurface_SetFont;
     thiz->GetFont = IDirectFBSurface_GetFont;
     thiz->DrawString = IDirectFBSurface_DrawString;
     thiz->DrawGlyph = IDirectFBSurface_DrawGlyph;
     thiz->SetEncoding = IDirectFBSurface_SetEncoding;

     thiz->GetSubSurface = IDirectFBSurface_GetSubSurface;

     thiz->GetGL = IDirectFBSurface_GetGL;

     thiz->Dump = IDirectFBSurface_Dump;
     thiz->DisableAcceleration = IDirectFBSurface_DisableAcceleration;
     thiz->SetDescription = IDirectFBSurface_SetDescription;
     thiz->SetCompositeCallback = IDirectFBSurface_SetCompositeCallback;
     thiz->SetDescriptionCallback = IDirectFBSurface_SetDescriptionCallback;

     if(surface )
     dfb_surface_attach( surface,
                         IDirectFBSurface_listener, thiz, &data->reaction );

     return DFB_OK;
}


/* internal */

static ReactionResult
IDirectFBSurface_listener( const void *msg_data, void *ctx )
{
     const CoreSurfaceNotification *notification = msg_data;
     IDirectFBSurface              *thiz         = ctx;
     IDirectFBSurface_data         *data         = thiz->priv;
     CoreSurface                   *surface      = data->surface;

     D_DEBUG_AT( Surface, "%s( %p, %p )\n", __FUNCTION__, msg_data, ctx );

     if (notification->flags & CSNF_DESTROY) {
          if (data->surface) {
               data->surface = NULL;
               dfb_surface_unref( surface );
          }
          return RS_REMOVE;
     }

     if (notification->flags & CSNF_SIZEFORMAT) {
          DFBRectangle rect = { 0, 0, surface->width, surface->height };
          
          dfb_rectangle_subtract( &rect, &data->area.insets );

          if (data->limit_set) {
               data->area.current = data->area.granted;

               dfb_rectangle_intersect( &data->area.current, &rect );
          }
          else
               data->area.wanted = data->area.granted = data->area.current = rect;

          /* Reset clip to avoid crashes caused by drawing out of bounds. */
          if (data->clip_set)
               thiz->SetClip( thiz, &data->clip_wanted );
          else
               thiz->SetClip( thiz, NULL );
     }

     return RS_OK;
}

