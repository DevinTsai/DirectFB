/*
   (c) Copyright 2001-2011  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
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

#ifndef __CORE__WINDOW_H__
#define __CORE__WINDOW_H__


#include <core/windows.h>


/**********************************************************************************************************************
 * CoreWindow
 */

/*
 * CoreWindow Calls
 */
typedef enum {
     CORE_WINDOW_SET_CONFIG        = 1,
     CORE_WINDOW_REPAINT           = 2,
} CoreWindowCall;

/*
 * CORE_WINDOW_SET_CONFIG
 */
typedef struct {
     CoreWindowConfig      config;
     CoreWindowConfigFlags flags;
} CoreWindowSetConfig;

/*
 * CORE_WINDOW_REPAINT
 */
typedef struct {
     DFBRegion             left;
     DFBRegion             right;
     DFBSurfaceFlipFlags   flags;
} CoreWindowRepaint;



DFBResult CoreWindow_SetConfig( CoreDFB                *core,
                                CoreWindow             *window,
                                const CoreWindowConfig *config,
                                CoreWindowConfigFlags   flags );

DFBResult CoreWindow_Repaint  ( CoreDFB                *core,
                                CoreWindow             *window,
                                const DFBRegion        *left,
                                const DFBRegion        *right,
                                DFBSurfaceFlipFlags     flags );


#endif

