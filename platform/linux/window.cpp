//**************************************************************
//*            OpenGLide - Glide to OpenGL Wrapper
//*             http://openglide.sourceforge.net
//*
//*    Linux specific functions for handling display window
//*
//*         OpenGLide is OpenSource under LGPL license
//*              Originaly made by Fabio Barros
//*      Modified by Paul for Glidos (http://www.glidos.net)
//*               Linux version by Simon White
//**************************************************************
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef C_USE_SDL

#include <stdio.h>
#include <unistd.h>
#include <GL/glx.h>
#include <X11/extensions/xf86vmode.h>

#include "GlOgl.h"

#include "platform/window.h"

#define _strtime(s) {time_t t = time(0); strftime(s, 99, "%H:%M:%S", localtime (&t));}
#define _strdate(s) {time_t t = time(0); strftime(s, 99, "%d %b %Y", localtime (&t));}
#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | \
		    PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )

static Display              *dpy          = NULL;
static int                   scrnum;
static Window                win;
static GLXContext            ctx          = NULL;
static bool                  vidmode_ext  = false;
static XF86VidModeModeInfo **vidmodes;
static bool                  mode_changed = false;
static FxU16                *aux_buffer   = 0;

void InitialiseOpenGLWindow(FxU wnd, int x, int y, int width, int height)
{
    Window root;
    XVisualInfo *visinfo = 0;
    XSetWindowAttributes attr;
    unsigned long mask;

    if (!(dpy = XOpenDisplay(NULL)))
    {
        fprintf(stderr, "Error couldn't open the X display\n");
        return;
    }

    scrnum = DefaultScreen(dpy);
    // win = (Window)wnd;
    root = RootWindow(dpy, scrnum);

#if 0
// Experiment with GLX 1.3 and the GLX_OML_swap_method extension
// Unable to verify operation as not supported by my video card
// If supported glXSwapBuffer can be called with no copying required
    {
        int attrib[] =
        {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_DEPTH_SIZE, DefaultDepth(dpy, scrnum),
            GLX_SWAP_EXCHANGE_OML,
            None
        };

        int elements = 0;
        GLXFBConfig *fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrib, &elements);
        visinfo = glXGetVisualFromFBConfig(dpy, *fbc);
    }
#endif

    if (!visinfo)
    {
        int attrib[] =
        {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_DEPTH_SIZE, DefaultDepth(dpy, scrnum),
            None
        };
        visinfo = glXChooseVisual(dpy, scrnum, attrib);
    }

    if (!visinfo)
    {
        fprintf(stderr, "Error couldn't get an RGB, Double-buffered, Depth visual\n");
        return;
    }

    {   // Determine presence of video mode extension
        int major = 0, minor = 0;
        vidmode_ext = XF86VidModeQueryVersion (dpy, &major, &minor) != 0;
    }
        
    if (vidmode_ext && UserConfig.InitFullScreen)
        mode_changed = SetScreenMode( width, height );

    // window attributes
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
    attr.event_mask = X_MASK;
    if (mode_changed)
    {
        mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore | 
               CWEventMask | CWOverrideRedirect;
        attr.override_redirect = True;
        attr.backing_store = NotUseful;
        attr.save_under = False;
    }
    else
        mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

    win = XCreateWindow(dpy, root, 0, 0, width, height,
                        0, visinfo->depth, InputOutput,
                        visinfo->visual, mask, &attr);
    XMapWindow(dpy, win);

    if (mode_changed)
    {
        XMoveWindow(dpy, win, 0, 0);
        XRaiseWindow(dpy, win);
        XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
        XFlush(dpy);
        // Move the viewport to top left
        XF86VidModeSetViewPort(dpy, scrnum, 0, 0);
    }

    XFlush(dpy);

    ctx = glXCreateContext(dpy, visinfo, NULL, True);

    glXMakeCurrent(dpy, win, ctx);

    GLint buffers = 0;
    glGetIntegerv(GL_AUX_BUFFERS, &buffers);

    if (!buffers)
        aux_buffer = (FxU16*) malloc (sizeof(FxU16) * width * height);
}

void FinaliseOpenGLWindow( void)
{
    if (dpy)
    {
        if (ctx)
            glXDestroyContext(dpy, ctx);
        if (win)
            XDestroyWindow(dpy, win);
        ResetScreenMode( );
        XCloseDisplay(dpy);
    }
    ctx = NULL;
    dpy = NULL;
    win = 0;
    aux_buffer = 0;
}

void SetGamma(float value)
{
}

void RestoreGamma()
{
}

bool SetScreenMode(int &width, int &height)
{
    int best_fit, best_dist, dist, x, y, i;
    int num_vidmodes;
    best_dist = 9999999;
    best_fit = -1;

    XF86VidModeGetAllModeLines(dpy, scrnum, &num_vidmodes, &vidmodes);

    for (i = 0; i < num_vidmodes; i++)
    {
        if (width > vidmodes[i]->hdisplay ||
            height > vidmodes[i]->vdisplay)
            continue;

        x = width - vidmodes[i]->hdisplay;
        y = height - vidmodes[i]->vdisplay;
        dist = (x * x) + (y * y);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_fit = i;
        }
    }

    if (best_fit != -1)
    {
        width  = vidmodes[best_fit]->hdisplay;
        height = vidmodes[best_fit]->vdisplay;

        // change to the mode
        XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[best_fit]);
        // Move the viewport to top left
        XF86VidModeSetViewPort(dpy, scrnum, 0, 0);
        return true;
    }

    if (vidmodes)
    {
        XFree(vidmodes);
        vidmodes=0;
    }

    return false;
}

void ResetScreenMode()
{
    if (mode_changed)
    {
        XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
        if (vidmodes)
        {
            XFree(vidmodes);
            vidmodes=0;
	}
    }
    if (aux_buffer)
        free (aux_buffer);
}

void SwapBuffers()
{
    // What a pain.  Under Glide front/back buffers are swapped.
    // Under Linux GL copies the back to front buffer and the
    // back buffer becomes underfined.  So we have to copy the
    // front buffer manually to the back (probably noticable
    // performance hit).  NOTE the restored image looks quantised.
    // Verify the screen mode used...
    glDisable( GL_BLEND );
    glDisable( GL_TEXTURE_2D );
    
    if (aux_buffer)
    {
        GLenum type = GL_UNSIGNED_SHORT_5_5_5_1_EXT;

        if ( InternalConfig.OGLVersion > OGL_VER_1_1 )
            type = GL_UNSIGNED_SHORT_5_6_5;

        glReadBuffer( GL_FRONT );
        glReadPixels( 0, 0, 
                      Glide.WindowWidth, Glide.WindowHeight,
                      GL_RGB, type, (void *)aux_buffer );

        glXSwapBuffers( dpy, win );

        glDrawBuffer( GL_BACK );
        glRasterPos2i(0, Glide.WindowHeight - 1);
        glDrawPixels( Glide.WindowWidth, Glide.WindowHeight, GL_RGB,
                      type, (void *)aux_buffer );
    }
    else
    {
        glReadBuffer(GL_FRONT); 
        glDrawBuffer(GL_AUX0);
        glRasterPos2i(0, Glide.WindowHeight - 1);
        glCopyPixels(0, 0, Glide.WindowWidth, Glide.WindowHeight, GL_COLOR);
	glXSwapBuffers(dpy, win);
        glReadBuffer(GL_AUX0);
        glDrawBuffer(GL_BACK);
        glRasterPos2i(0, Glide.WindowHeight - 1);
        glCopyPixels(0, 0, Glide.WindowWidth, Glide.WindowHeight, GL_COLOR);
    }

    if ( OpenGL.Blend )
        glEnable( GL_BLEND );
    glDrawBuffer( OpenGL.RenderBuffer );
}

#endif // C_USE_SDL
