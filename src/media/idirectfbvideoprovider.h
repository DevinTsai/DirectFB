/*
   (c) Copyright 2000-2002  convergence integrated media GmbH.
   (c) Copyright 2002-2005  convergence GmbH.

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrj�l� <syrjala@sci.fi> and
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
/*
 * (c) Copyright 2004-2006 Mitsubishi Electric Corp.
 *
 * All rights reserved.
 *
 * Written by Koichi Hiramatsu,
 *            Seishi Takahashi,
 *            Atsushi Hori
 */

#ifndef __IDIRECTFBVIDEOPROVIDER_H__
#define __IDIRECTFBVIDEOPROVIDER_H__

#ifdef DFB_ARIB
#include <dfb_types.h>
#include <core/coretypes.h>
#endif

/*
 * probing context
 */
typedef struct {
     unsigned char        header[64];

     /* Only set if databuffer is created from file. */
     const char          *filename;

     /* Usefull if provider needs more data for probing. */
     IDirectFBDataBuffer *buffer;
#ifdef DFB_ARIB
     CoreLayer        *layer;
     CoreLayerContext *context;
#endif
} IDirectFBVideoProvider_ProbeContext;


DFBResult
IDirectFBVideoProvider_CreateFromBuffer( IDirectFBDataBuffer     *buffer,
                                         IDirectFBVideoProvider **interface );

#endif

