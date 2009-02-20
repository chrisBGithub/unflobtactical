#include "uirendering.h"
#include "platformgl.h"
using namespace grinliz;

void UFODrawIcons( const IconInfo* icons, int width, int height, int rotation )
{
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glEnable( GL_BLEND );
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();					// save projection
	glLoadIdentity();				// projection

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();					// model
	glLoadIdentity();				// model

	glRotatef( 90.0f * (float)rotation, 0.0f, 0.0f, 1.0f );
#ifdef __APPLE__
	glOrthof( 0.f, (float)width, 0.f, (float)height, -1.0f, 1.0f );
#else
	glOrtho( 0, width, 0, height, -1, 1 );
#endif

	int16_t v[8];
	float t[8];

	while( icons && icons->size.x > 0 )
	{
		t[0] = icons->tMin.x;				t[1] = icons->tMin.y;
		t[2] = icons->tMax.x;				t[3] = icons->tMin.y;
		t[4] = icons->tMax.x;				t[5] = icons->tMax.y;
		t[6] = icons->tMin.x;				t[7] = icons->tMax.y;

		v[0] = icons->pos.x;				v[1] = icons->pos.y;
		v[2] = icons->pos.x+icons->size.x;	v[3] = icons->pos.y;
		v[4] = icons->pos.x+icons->size.x;	v[5] = icons->pos.y+icons->size.y;
		v[6] = icons->pos.x;				v[7] = icons->pos.y+icons->size.y;

		glVertexPointer(   2, GL_SHORT, 0, v );
		glTexCoordPointer( 2, GL_FLOAT, 0, t );  

		CHECK_GL_ERROR;
		glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
		
		++icons;
	}


	glPopMatrix();					// model
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();					// projection
	glMatrixMode(GL_MODELVIEW);

	glDisable( GL_BLEND );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
}