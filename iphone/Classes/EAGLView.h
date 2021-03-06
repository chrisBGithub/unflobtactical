//
//  EAGLView.h
//  ufoattack
//
//  Created by Lee Thomason on 10/12/08.
//  Copyright __MyCompanyName__ 2008. All rights reserved.
//


#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES1/gl.h>
#import <OpenGLES/ES1/glext.h>
#import "../../game/cgame.h"

/*
This class wraps the CAEAGLLayer from CoreAnimation into a convenient UIView subclass.
The view content is basically an EAGL surface you render your OpenGL scene into.
Note that setting the view non-opaque will only work if the EAGL surface has an alpha channel.
*/
@interface EAGLView : UIView {
    
@private
    /* The pixel dimensions of the backbuffer */
    GLint backingWidth;
    GLint backingHeight;
    
    EAGLContext *context;
    
    /* OpenGL names for the renderbuffer and framebuffers used to render to this view */
    GLuint viewRenderbuffer, viewFramebuffer;
    
    /* OpenGL name for the depth buffer that is attached to viewFramebuffer, if it exists (0 if it does not exist) */
    GLuint depthRenderbuffer;
    
    NSTimer *animationTimer;
    NSTimeInterval animationInterval;
	
	void* game;
	double startTime;
	
	bool isDragging;
	bool isZooming;
	bool isMoving;
	float orbitStart;
	float previousRotation;
	CGPoint lastDrag;
}

@property NSTimeInterval animationInterval;

- (void)startAnimation;
- (void)stopAnimation;
- (void)drawView;
- (float)calcLength:(CGPoint)p0 p1:(CGPoint)p1;
- (void)dumpTouch:(UITouch*)touch;
- (NSString*) getSavePath;
@end
