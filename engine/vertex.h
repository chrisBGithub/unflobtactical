#ifndef UFOATTACK_VERTEX_INCLUDED
#define UFOATTACK_VERTEX_INCLUDED

#include <vector>
#include "../grinliz/glvector.h"
#include "../grinliz/glmatrix.h"

class Texture;


struct Vertex
{
	enum {
		POS_OFFSET = 0,
		NORMAL_OFFSET = 12,
		TEXTURE_OFFSET = 24
	};

	grinliz::Vector3F	pos;
	grinliz::Vector3F	normal;
	grinliz::Vector2F	tex;

	bool Equal( const Vertex& v ) const {
		if (    pos == v.pos 
			 && normal == v.normal
			 && tex == v.tex )
		{
			return true;
		}
		return false;
	}
};


#endif //  UFOATTACK_VERTEX_INCLUDED