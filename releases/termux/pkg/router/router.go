package router

/*
#cgo CFLAGS: -I${SRCDIR}/../../router/include
#cgo LDFLAGS: -L${SRCDIR}/../../build/router -lrazor_router_shared -lstdc++ -lm -Wl,-rpath,${SRCDIR}/../../build/router
#include "c_router_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"runtime"
	"unsafe"
)

type RouteResult struct {
	Category   string
	CacheHit   bool
	Confidence float32
}

type Router struct {
	handle C.RazorRouterHandle
}

func NewRouter(cacheCapacity int, cacheTTLSec int) (*Router, error) {
	handle := C.RazorRouter_Create(C.size_t(cacheCapacity), C.int(cacheTTLSec))
	if handle == nil {
		return nil, fmt.Errorf("failed to create RazorRouter handle")
	}

	r := &Router{handle: handle}
	runtime.SetFinalizer(r, func(obj *Router) {
		obj.Close()
	})
	return r, nil
}

func (r *Router) Close() {
	if r.handle != nil {
		C.RazorRouter_Destroy(r.handle)
		r.handle = nil
	}
}

func (r *Router) Route(prompt string) (*RouteResult, error) {
	if r.handle == nil {
		return nil, fmt.Errorf("router handle is closed")
	}

	cPrompt := C.CString(prompt)
	defer C.free(unsafe.Pointer(cPrompt))

	var cCategory *C.char
	var cCacheHit C.int
	var cConfidence C.float

	res := C.RazorRouter_Route(r.handle, cPrompt, &cCategory, &cCacheHit, &cConfidence)
	if res != 0 {
		return nil, fmt.Errorf("router routing error code: %d", res)
	}

	defer C.RazorRouter_FreeString(cCategory)

	category := C.GoString(cCategory)
	return &RouteResult{
		Category:   category,
		CacheHit:   cCacheHit != 0,
		Confidence: float32(cConfidence),
	}, nil
}

func (r *Router) GetCacheSize() int {
	if r.handle == nil {
		return 0
	}
	return int(C.RazorRouter_GetCacheSize(r.handle))
}

func (r *Router) ClearCache() {
	if r.handle != nil {
		C.RazorRouter_ClearCache(r.handle)
	}
}
