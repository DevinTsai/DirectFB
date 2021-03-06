/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

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



#ifndef __IDIRECTFBIMAGEPROVIDER_H__
#define __IDIRECTFBIMAGEPROVIDER_H__

#include <core/coretypes.h>

/*
 * probing context
 */
typedef struct {
     unsigned char  header[32];

     const char    *filename; /* Only set if databuffer is created from file. */
} IDirectFBImageProvider_ProbeContext;

DFBResult
IDirectFBImageProvider_CreateFromBuffer( IDirectFBDataBuffer     *buffer,
                                         CoreDFB                 *core,
                                         IDirectFB               *idirectfb,
                                         IDirectFBImageProvider **interface_ptr );

/**********************************************************************************************************************/

/*
 * private data struct of IDirectFBImageProvider
 * used by implementors of IDirectFBImageProvider
 */
typedef struct {
     int                  ref; /* reference counter */

     IDirectFBDataBuffer *buffer;

     CoreDFB             *core;
     IDirectFB           *idirectfb;

     DIRenderCallback     render_callback;
     void                *render_callback_context;

     void (*Destruct)( IDirectFBImageProvider *thiz );
} IDirectFBImageProvider_data;


#endif
