#include <video/GLDriver.h>
#include <core/LinuxFramework.h>
#include <stdio.h>

#include <sys/time.h>
extern int g_AvoidInput;
extern std::vector<std::string> g_args;
namespace t800 {
#ifdef USING_WAYLAND_NATIVE
struct wl_compositor    *wlnd_compositor = 0;
struct wl_shell         *wlnd_shell = 0;

static void registry_add_object(void *data, struct wl_registry *registry,
                     uint32_t name, const char *interface, uint32_t version){
    if (!strcmp(interface,"wl_compositor")) {
        wlnd_compositor = (wl_compositor*)wl_registry_bind (registry, name, &wl_compositor_interface, 1);
    }else if (strcmp(interface, "wl_shell") == 0) {
        wlnd_shell = (wl_shell*)wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_remove_object(void *data,
        struct wl_registry *registry, uint32_t name)
{}

static struct wl_registry_listener registry_listener = {
                                                    registry_add_object,
                                                    registry_remove_object
                                                    };

extern void EGLError(const char* c_ptr);
#endif

LinuxFramework *LinuxFramework::thiz = 0;

LinuxFramework::LinuxFramework(AppBase *pBaseApp) : RootFramework(pBaseApp), m_alive(true) 	{
		pBaseApp->SetParentFramework(this);
		LinuxFramework::thiz = this;
}

void LinuxFramework::InitGlobalVars(){

}

#include <iostream>
#include <string>
#include <vector>




void LinuxFramework::OnCreateApplication(ApplicationDesc desc){
    aplicationDescriptor = desc;
    m_inited = true;
#ifdef USING_WAYLAND_NATIVE 
    printf("Using Wayland Camus\n");
#else
    printf("Using Wayland Freeglut\n");
#endif

    int req = 0;
    g_AvoidInput = 0;
    putenv( (char *) "__GL_SYNC_TO_VBLANK=1" );
    int width = 1280;
	int height = 720;

	for(unsigned int i=0;i<g_args.size();i++){
        if(g_args[i] == "-x"){
           width = atoi( g_args[i+1].c_str() );
        }

        if(g_args[i] == "-y"){
           height = atoi( g_args[i+1].c_str() );
        }
	}
#ifndef T850_HEADLESS
#ifdef USING_FREEGLUT
    glutInit(&req,0);
    glutInitContextProfile(GLUT_CORE_PROFILE);
#ifdef USING_OPENGL_ES30
    glutInitContextVersion(3,0);
#elif defined(USING_OPENGL_ES31)
    glutInitContextVersion(3,1);
#elif defined(USING_OPENGL_ES20)
    glutInitContextVersion(2,0);
#endif


    glutInitWindowSize(width,height);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB|GLUT_DEPTH);
    glutCreateWindow("T800");
    glutDisplayFunc(IdleFunction);
    glutIdleFunc(IdleFunction);
    glutMouseFunc(MouseClickFunction);
    glutPassiveMotionFunc(MouseMoveFunction);
    glutReshapeFunc(ResizeWindow);
    glutKeyboardFunc(KeyboardEvent);
    glutKeyboardUpFunc(KeyboardReleaseEvent);
#elif defined(USING_WAYLAND_NATIVE)

    wlnd_display = wl_display_connect(NULL);
    if (wlnd_display == NULL) {
        fprintf(stderr, "Can't connect to display\n");
        exit(1);
    }
    printf("connected to display\n");

    wl_registry *registry = wl_display_get_registry(wlnd_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(wlnd_display);
    wl_display_roundtrip(wlnd_display);

    if (wlnd_compositor == NULL) {
        fprintf(stderr, "Can't find compositor\n");
	exit(1);
    } else {
        fprintf(stderr, "Found compositor\n");
    }

    wlnd_surface = wl_compositor_create_surface(wlnd_compositor);
    if (wlnd_surface == NULL) {
        fprintf(stderr, "Can't create surface\n");
        exit(1);
    } else {
        fprintf(stderr, "Created surface\n");
    }

    wlnd_shell_surface = wl_shell_get_shell_surface(wlnd_shell, wlnd_surface);
    wl_shell_surface_set_toplevel(wlnd_shell_surface);

    wland_region = wl_compositor_create_region(wlnd_compositor);
    wl_region_add(wland_region, 0, 0,
		  width,
		  height);
    wl_surface_set_opaque_region(wlnd_surface, wland_region);




	EGLint numConfigs;

	eglDisplay = eglGetDisplay((EGLNativeDisplayType)wlnd_display);

	EGLError("eglGetDisplay");

	EGLint iMajorVersion, iMinorVersion;

	if (!eglInitialize(eglDisplay, &iMajorVersion, &iMinorVersion)) {
		std::cout << "Failed to initialize egl" << std::endl;
	}else{
		std::cout << "EGL version " << iMajorVersion << "." << iMinorVersion << std::endl;
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	EGLError("eglBindAPI");

	const EGLint attribs[] = {
		EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE,		8,
		EGL_GREEN_SIZE,		8,
		EGL_RED_SIZE,		8,
		EGL_DEPTH_SIZE,		24,
		EGL_NONE
	};

	if(!eglChooseConfig(eglDisplay, attribs, &eglConfig, 1, &numConfigs)){
		std::cout << "Failed to choose config" << std::endl;
	}

	EGLError("eglChooseConfig");

	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	eglContext = eglCreateContext(eglDisplay, eglConfig, NULL, ai32ContextAttribs);

	EGLError("eglCreateContext");

	wland_egl_window = wl_egl_window_create(wlnd_surface,width, height);
    if (wland_egl_window == EGL_NO_SURFACE) {
        fprintf(stderr, "Can't create egl window\n");
	exit(1);
    } else {
        fprintf(stderr, "Created egl window\n");
    }

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType) wland_egl_window, NULL);

	EGLError("eglCreateWindowSurface");

	if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == EGL_FALSE) {
		std::cout << "Failed to make current" << std::endl;
		return;
	}
#endif
#endif //headless
    pVideoDriver = new GLDriver;
    pVideoDriver->SetDimensions(width, height);

    g_pBaseDriver = pVideoDriver;

	pVideoDriver->SetWindow(0);
	pVideoDriver->InitDriver();

	timeval start;
    gettimeofday(&start,0);


	pBaseApp->InitVars();
	pBaseApp->CreateAssets();

	timeval actual;
	gettimeofday(&actual,0);
	double ttaken = double( (actual.tv_sec - start.tv_sec) + (actual.tv_usec - start.tv_usec)/1000000.0);

	printf("Asset Loading took : %f \n",ttaken);

#ifdef USING_FREEGLUT
	glutMainLoop();
#elif defined(USING_WAYLAND_NATIVE)
    UpdateApplication();
#endif // USING_FREEGLUT
}

void LinuxFramework::OnDestroyApplication(){

}

void LinuxFramework::OnInterruptApplication(){

}

void LinuxFramework::OnResumeApplication(){

}

void LinuxFramework::UpdateApplication(){
#ifdef USING_FREEGLUT
    pBaseApp->OnUpdate();
#elif defined(USING_WAYLAND_NATIVE)
    while(m_alive){
        pBaseApp->OnUpdate();
        eglSwapBuffers(eglDisplay, eglSurface);
    }
#endif
}

void LinuxFramework::ProcessInput(){

}

void LinuxFramework::ResetApplication(){

}
void LinuxFramework::ChangeAPI(GRAPHICS_API::E api){

}

#ifdef USING_FREEGLUT
// STATIC
void LinuxFramework::IdleFunction(){
    thiz->UpdateApplication();
    thiz->pBaseApp->IManager.xDelta = 0;
    thiz->pBaseApp->IManager.yDelta = 0;
}

void LinuxFramework::MouseClickFunction(int button, int state, int x, int y){

}

void LinuxFramework::MouseMoveFunction(int x, int y){
    static int xDelta = 0;
	static int yDelta = 0;

	xDelta = x - xDelta;
	yDelta = y - yDelta;

	thiz->pBaseApp->IManager.xDelta = xDelta;
	thiz->pBaseApp->IManager.yDelta = yDelta;

	xDelta = x;
	yDelta = y;
}

void LinuxFramework::ResizeWindow(int w, int h){

}

void LinuxFramework::KeyboardEvent(unsigned char key, int x, int y){
printf("key %d \n",key);
	thiz->pBaseApp->IManager.KeyStates[0][key] = true;
}

void LinuxFramework::KeyboardReleaseEvent(unsigned char key, int x, int y){
	thiz->pBaseApp->IManager.KeyStates[0][key] = false;
	thiz->pBaseApp->IManager.KeyStates[1][key] = false;
}
#endif
}