/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#if defined(__APPLE__)
#include "SDL_image.h"
#endif
#include "SDL_loadso.h"

#include "../grinliz/gldebug.h"
#include "../grinliz/gltypes.h"
#include "../engine/enginelimits.h"

#include <string>
#include <vector>

using namespace std;

#include "../grinliz/gldynamic.h"
#include "../grinliz/glutil.h"
#include "../grinliz/glstringutil.h"
#include "../tinyxml/tinyxml.h"

#include "modelbuilder.h"
#include "../engine/serialize.h"
#include "../importers/import.h"

#include "../sqlite3/sqlite3.h"
#include "../shared/gldatabase.h"


typedef SDL_Surface* (SDLCALL * PFN_IMG_LOAD) (const char *file);
PFN_IMG_LOAD libIMG_Load;

string outputPath;
string outputDB;
vector<U16> pixelBuffer16;
vector<U8> pixelBuffer8;

string inputPath;
int totalModelMem = 0;
int totalTextureMem = 0;
int totalMapMem = 0;

sqlite3* db = 0;
BinaryDBWriter *writer = 0;



void CreateDatabase()
{
	int sqlResult = 0;

	// Set up the database tables.
	sqlResult = sqlite3_exec(	db, 
								"CREATE TABLE texture "
								"(name TEXT NOT NULL PRIMARY KEY, "
								" format TEXT, image INT, width INT, height INT, id INT);",
								0, 0, 0 );
	GLASSERT( sqlResult == SQLITE_OK );

	sqlResult	= sqlite3_exec(	db,
								"CREATE TABLE model "
								"(name TEXT NOT NULL PRIMARY KEY, "
								" headerID INT, "
								" groupStart INT, nGroup INT, "
								" vertexID INT, indexID INT );",
								0, 0, 0 );
	GLASSERT( sqlResult == SQLITE_OK );

	sqlResult	= sqlite3_exec(	db,
								"CREATE TABLE modelGroup "
								"(id INT NOT NULL PRIMARY KEY, "
								" textureName TEXT, "
								" nVertex INT, nIndex INT );",
								0, 0, 0 );
	GLASSERT( sqlResult == SQLITE_OK );

	sqlResult	= sqlite3_exec(	db,
								"CREATE TABLE map "
								"(name TEXT NOT NULL PRIMARY KEY, "
								" id INT );",
								0, 0, 0 );
	GLASSERT( sqlResult == SQLITE_OK );

}


void InsertTextureToDB( const char* name, const char* format, bool isImage, int width, int height, const void* pixels, int sizeInBytes )
{
	int index = 0;
	writer->Write( pixels, sizeInBytes, &index );

	sqlite3_stmt* stmt = NULL;
	sqlite3_prepare_v2( db, "INSERT INTO texture VALUES (?, ?, ?, ?, ?, ?);", -1, &stmt, 0 );

	sqlite3_bind_text( stmt, 1, name, -1, SQLITE_TRANSIENT );
	sqlite3_bind_text( stmt, 2, format, -1, SQLITE_TRANSIENT );
	sqlite3_bind_int( stmt, 3, isImage ? 1 : 0 );
	sqlite3_bind_int( stmt, 4, width );
	sqlite3_bind_int( stmt, 5, height );
	sqlite3_bind_int( stmt, 6, index );

	sqlite3_step( stmt );
	sqlite3_finalize(stmt);
}


void InsertModelHeaderToDB( const ModelHeader& header, int groupID, int vertexID, int indexID )
{
	int index = 0;
	writer->Write( &header, sizeof(header), &index );

	sqlite3_stmt* stmt = NULL;

	sqlite3_prepare_v2( db, "INSERT INTO model VALUES (?,?,?,?,?,?);", -1, &stmt, 0 );
	sqlite3_bind_text( stmt, 1,	header.name, -1, SQLITE_TRANSIENT );
	sqlite3_bind_int(  stmt, 2, index );
	sqlite3_bind_int(  stmt, 3, groupID );
	sqlite3_bind_int(  stmt, 4, header.nGroups );
	sqlite3_bind_int(  stmt, 5, vertexID );
	sqlite3_bind_int(  stmt, 6, indexID );
	sqlite3_step( stmt );
	sqlite3_finalize(stmt);
}


void InsertModelGroupToDB( const ModelGroup& group, int *groupID )
{
	static int groupIDPool = 1;
	sqlite3_stmt* stmt = NULL;
	*groupID = groupIDPool;

	sqlite3_prepare_v2( db, "INSERT INTO modelGroup VALUES (?,?,?,?);", -1, &stmt, 0 );
	sqlite3_bind_int(  stmt, 1, groupIDPool );
	sqlite3_bind_text( stmt, 2,	group.textureName, -1, SQLITE_TRANSIENT );
	sqlite3_bind_int(  stmt, 3, group.nVertex );
	sqlite3_bind_int(  stmt, 4, group.nIndex );
	sqlite3_step( stmt );
	sqlite3_finalize(stmt);

	++groupIDPool;
}


void LoadLibrary()
{
	#if defined(__APPLE__)
		libIMG_Load = &IMG_Load;
	#else
		void* handle = grinliz::grinlizLoadLibrary( "SDL_image" );
		if ( !handle )
		{	
			exit( 1 );
		}
		libIMG_Load = (PFN_IMG_LOAD) grinliz::grinlizLoadFunction( handle, "IMG_Load" );
		GLASSERT( libIMG_Load );
	#endif
}



void WriteFloat( SDL_RWops* ctx, float f )
{
	SDL_RWwrite( ctx, &f, sizeof(float), 1 );
}



void ModelHeader::Set(	const char* name, int nGroups, int nTotalVertices, int nTotalIndices,
						const grinliz::Rectangle3F& bounds )
{
	GLASSERT( nGroups > 0 && nGroups < EL_MAX_MODEL_GROUPS );
	GLASSERT( nTotalVertices > 0 && nTotalVertices < EL_MAX_VERTEX_IN_MODEL );
	GLASSERT( EL_MAX_VERTEX_IN_MODEL <= 0xffff );
	GLASSERT( nTotalIndices > 0 && nTotalIndices < EL_MAX_INDEX_IN_MODEL );
	GLASSERT( EL_MAX_INDEX_IN_MODEL <= 0xffff );
	GLASSERT( nTotalVertices <= nTotalIndices );

	memset( this->name, 0, EL_FILE_STRING_LEN );
	strncpy( this->name, name, EL_FILE_STRING_LEN );
	this->flags = 0;
	this->nGroups = nGroups;
	this->nTotalVertices = nTotalVertices;
	this->nTotalIndices = nTotalIndices;
	this->bounds = bounds;
	trigger.Set( 0, 0, 0 );
	eye = 0;
	target = 0;
}


void ModelGroup::Set( const char* textureName, int nVertex, int nIndex )
{
	GLASSERT( nVertex > 0 && nVertex < EL_MAX_VERTEX_IN_MODEL );
	GLASSERT( nIndex > 0 && nIndex < EL_MAX_INDEX_IN_MODEL );
	GLASSERT( nVertex <= nIndex );

	memset( this->textureName, 0, EL_FILE_STRING_LEN );
	strncpy( this->textureName, textureName, EL_FILE_STRING_LEN );

	this->nVertex = nVertex;
	this->nIndex = nIndex;
}


U32 GetPixel(SDL_Surface *surface, int x, int y)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    U8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        return *p;

    case 2:
        return *(Uint16 *)p;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;

    case 4:
        return *(U32 *)p;

    default:
        return 0;       /* shouldn't happen, but avoids warnings */
    }
}


void ProcessMap( TiXmlElement* map )
{
	string filename;
	map->QueryStringAttribute( "filename", &filename );
	string fullIn = inputPath + filename;

	string name;
	grinliz::StrSplitFilename( fullIn, 0, &name, 0 );

	// copy the entire file.
	FILE* read = fopen( fullIn.c_str(), "rb" );
	if ( !read ) {
		printf( "**Unrecognized map file. '%s'\n",
				 fullIn.c_str() );
		exit( 1 );
	}

	// length of file.
	fseek( read, 0, SEEK_END );
	int len = ftell( read );
	fseek( read, 0, SEEK_SET );

	char* mem = new char[len];
	fread( mem, len, 1, read );
	//fwrite( mem, len, 1, write );

	int index = 0;
	writer->Write( mem, len, &index );

	sqlite3_stmt* stmt = NULL;
	sqlite3_prepare_v2( db, "INSERT INTO map VALUES (?,?);", -1, &stmt, 0 );
	sqlite3_bind_text( stmt, 1,	name.c_str(), -1, SQLITE_TRANSIENT );
	sqlite3_bind_int(  stmt, 2, index );
	sqlite3_step( stmt );
	sqlite3_finalize(stmt);

	delete [] mem;

	printf( "Map '%s' memory=%dk\n", filename.c_str(), len/1024 );
	totalMapMem += len;

	fclose( read );
}


void ProcessModel( TiXmlElement* model )
{
	int nTotalIndex = 0;
	int nTotalVertex = 0;
	int startIndex = 0;
	int startVertex = 0;

	printf( "Model " );

	string filename;
	model->QueryStringAttribute( "filename", &filename );
	string fullIn = inputPath + filename;

	string base, name, extension;
	grinliz::StrSplitFilename( fullIn, &base, &name, &extension );
	//string fullOut = outputPath + name + ".mod";

	bool smoothShading = false;
	if ( grinliz::StrEqual( model->Attribute( "shading" ), "smooth" ) ) {
		smoothShading = true;
	}

	ModelBuilder* builder = new ModelBuilder();
	builder->SetShading( smoothShading );

	if ( extension == ".ac" ) {
		ImportAC3D(	fullIn, builder );
	}
	else if ( extension == ".off" ) {
		ImportOFF( fullIn, builder );
	}
	else {
		printf( "**Unrecognized model file. full='%s' base='%s' name='%s' extension='%s'\n",
				 fullIn.c_str(), base.c_str(), name.c_str(), extension.c_str() );
		exit( 1 );
	}

	const VertexGroup* vertexGroup = builder->Groups();

	//SDL_RWops* fp = SDL_RWFromFile( fullOut.c_str(), "wb" );
	//if ( !fp ) {
	//	printf( "**Could not open for writing: %s\n", fullOut.c_str() );
	//	exit( 1 );
	//}
	//else {
	//	//printf( "  Writing: '%s', '%s'", name.c_str(), fullOut.c_str() );
	//	printf( "  '%s'", name.c_str() );
	//}
	
	for( int i=0; i<builder->NumGroups(); ++i ) {
		nTotalIndex += vertexGroup[i].nIndex;
		nTotalVertex += vertexGroup[i].nVertex;
	}
	printf( " groups=%d nVertex=%d nTri=%d\n", builder->NumGroups(), nTotalVertex, nTotalIndex/3 );

	
	ModelHeader header;
	header.Set( name.c_str(), builder->NumGroups(), nTotalVertex, nTotalIndex, builder->Bounds() );

	if ( grinliz::StrEqual( model->Attribute( "billboard" ), "true" ) ) {
		header.flags |= ModelHeader::BILLBOARD;
		// Make the bounds square.
		float d = grinliz::Max( -header.bounds.min.x, -header.bounds.min.z, header.bounds.max.x, header.bounds.max.z );
		header.bounds.min.x = -d;
		header.bounds.min.z = -d;
		header.bounds.max.x = d;
		header.bounds.max.z = d;
	}
	if ( grinliz::StrEqual( model->Attribute( "shadow" ), "rotate" ) ) {
		header.flags |= ModelHeader::ROTATE_SHADOWS;
	}
	if ( grinliz::StrEqual( model->Attribute( "origin" ), "upperLeft" ) ) {
		header.flags |= ModelHeader::UPPER_LEFT;
	}
	if ( model->Attribute( "trigger" ) ) {
		sscanf( model->Attribute( "trigger" ), "%f %f %f", &header.trigger.x, &header.trigger.y, &header.trigger.z );
	}
	if ( model->Attribute( "eye" ) ) {
		model->QueryFloatAttribute( "eye", &header.eye );
	}
	if ( model->Attribute( "target" ) ) {
		model->QueryFloatAttribute( "target", &header.target );
	}

	//header.Save( fp );
	int groupID = 0;
	int totalMemory = 0;

	for( int i=0; i<builder->NumGroups(); ++i ) {
		int mem = vertexGroup[i].nVertex*sizeof(Vertex) + vertexGroup[i].nIndex*2;
		totalMemory += mem;
		printf( "    %d: '%s' nVertex=%d nTri=%d memory=%.1fk\n",
				i,
				vertexGroup[i].textureName,
				vertexGroup[i].nVertex,
				vertexGroup[i].nIndex / 3,
				(float)mem/1024.0f );

		ModelGroup group;
		group.Set( vertexGroup[i].textureName, vertexGroup[i].nVertex, vertexGroup[i].nIndex );
		//group.Save( fp );
		int id=0;
		InsertModelGroupToDB( group, &id );
		if ( i==0 )
			groupID = id;

		startVertex += vertexGroup[i].nVertex;
		startIndex += vertexGroup[i].nIndex;
	}

	Vertex* vertexBuf = new Vertex[nTotalVertex];
	U16* indexBuf = new U16[nTotalIndex];

	Vertex* pVertex = vertexBuf;
	U16* pIndex = indexBuf;
	
	const Vertex* pVertexEnd = vertexBuf + nTotalVertex;
	const U16* pIndexEnd = indexBuf + nTotalIndex;

	// Write the vertices in each group:
	for( int i=0; i<builder->NumGroups(); ++i ) {
		//SDL_RWwrite( fp, vertexGroup[i].vertex, sizeof(Vertex), vertexGroup[i].nVertex );

		GLASSERT( pVertex + vertexGroup[i].nVertex <= pVertexEnd );
		memcpy( pVertex, vertexGroup[i].vertex, sizeof(Vertex)*vertexGroup[i].nVertex );
		pVertex += vertexGroup[i].nVertex;
	}
	// Write the indices in each group:
	for( int i=0; i<builder->NumGroups(); ++i ) {
		//SDL_RWwrite( fp, vertexGroup[i].index, sizeof(U16), vertexGroup[i].nIndex );

		GLASSERT( pIndex + vertexGroup[i].nIndex <= pIndexEnd );
		memcpy( pIndex, vertexGroup[i].index, sizeof(U16)*vertexGroup[i].nIndex );
		pIndex += vertexGroup[i].nIndex;
	}
	GLASSERT( pVertex == pVertexEnd );
	GLASSERT( pIndex == pIndexEnd );

	int vertexID = 0, indexID = 0;

	writer->Write( vertexBuf, nTotalVertex*sizeof(Vertex), &vertexID );
	writer->Write( indexBuf,  nTotalIndex*sizeof(U16),     &indexID );
	InsertModelHeaderToDB( header, groupID, vertexID, indexID );

	printf( "  total memory=%.1fk\n", (float)totalMemory / 1024.f );
	totalModelMem += totalMemory;
	
	delete [] vertexBuf;
	delete [] indexBuf;
	delete builder;
	//if ( fp ) {
	//	SDL_FreeRW( fp );
	//}
}


void ProcessTexture( TiXmlElement* texture )
{
	bool isImage = false;
	if ( texture->ValueStr() == "image" ) {
		isImage = true;
		printf( "Image" );
	}
	else {
		printf( "Texture" );
	}

	string filename;
	texture->QueryStringAttribute( "filename", &filename );

	string fullIn = inputPath + filename;	

	string base, name, extension;
	grinliz::StrSplitFilename( fullIn, &base, &name, &extension );

	SDL_Surface* surface = libIMG_Load( fullIn.c_str() );
	if ( !surface ) {
		printf( "**Could not load: %s\n", fullIn.c_str() );
		exit( 1 );
	}
	else {
		printf( "  Loaded: '%s' bpp=%d width=%d height=%d", 
				name.c_str(), //fullIn.c_str(),
				surface->format->BitsPerPixel,
				surface->w,
				surface->h );
	}

	U8 r, g, b, a;
	pixelBuffer16.resize(0);
	pixelBuffer8.resize(0);

	switch( surface->format->BitsPerPixel ) {
		case 32:
			printf( "  RGBA memory=%dk\n", (surface->w * surface->h * 2)/1024 );
			totalTextureMem += (surface->w * surface->h * 2);
			//header.Set( name.c_str(), GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, surface->w, surface->h );
			//header.Save( fp );

			// Bottom up!
			for( int j=surface->h-1; j>=0; --j ) {
				for( int i=0; i<surface->w; ++i ) {
					U32 c = GetPixel( surface, i, j );
					SDL_GetRGBA( c, surface->format, &r, &g, &b, &a );

					U16 p =
							  ( ( r>>4 ) << 12 )
							| ( ( g>>4 ) << 8 )
							| ( ( b>>4 ) << 4)
							| ( ( a>>4 ) << 0 );

					pixelBuffer16.push_back(p);
				}
			}
			InsertTextureToDB( name.c_str(), "RGBA16", isImage, surface->w, surface->h, &pixelBuffer16[0], pixelBuffer16.size()*2 );
			break;

		case 24:
			printf( "  RGB memory=%dk\n", (surface->w * surface->h * 2)/1024 );
			totalTextureMem += (surface->w * surface->h * 2);
			//header.Set( name.c_str(), GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->w, surface->h );
			//header.Save( fp );

			// Bottom up!
			for( int j=surface->h-1; j>=0; --j ) {
				for( int i=0; i<surface->w; ++i ) {
					U32 c = GetPixel( surface, i, j );
					SDL_GetRGBA( c, surface->format, &r, &g, &b, &a );

					U16 p = 
							  ( ( r>>3 ) << 11 )
							| ( ( g>>2 ) << 5 )
							| ( ( b>>3 ) );

					//SDL_WriteLE16( fp, p );
					pixelBuffer16.push_back(p);
				}
			}
			InsertTextureToDB( name.c_str(), "RGB16", isImage, surface->w, surface->h, &pixelBuffer16[0], pixelBuffer16.size()*2 );
			break;

		case 8:
			printf( "  Alpha memory=%dk\n", (surface->w * surface->h * 1)/1024 );
			totalTextureMem += (surface->w * surface->h * 1);
			//header.Set( name.c_str(), GL_ALPHA, GL_UNSIGNED_BYTE, surface->w, surface->h );
			//header.Save( fp );

			// Bottom up!
			for( int j=surface->h-1; j>=0; --j ) {
				for( int i=0; i<surface->w; ++i ) {
				    U8 *p = (U8 *)surface->pixels + j*surface->pitch + i;
					//SDL_RWwrite( fp, p, 1, 1 );
					pixelBuffer8.push_back(*p);
				}
			}
			InsertTextureToDB( name.c_str(), "ALPHA", isImage, surface->w, surface->h, &pixelBuffer8[0], pixelBuffer8.size() );
			break;

		default:
			printf( "Unsupported bit depth!\n" );
			exit( 1 );
			break;
	}

	if ( surface ) { 
		SDL_FreeSurface( surface );
	}
	//if ( fp ) {
	//	SDL_FreeRW( fp );
	//}

}


int main( int argc, char* argv[] )
{
	printf( "UFO Builder. argc=%d argv[1]=%s\n", argc, argv[1] );
	if ( argc < 3 ) {
		printf( "Usage: ufobuilder ./inputPath/ inputXMLName\n" );
		exit( 1 );
	}

    SDL_Init( SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE | SDL_INIT_TIMER );
	LoadLibrary();

	inputPath = argv[1];
	const char* xmlfile = argv[2];
	printf( "Opening, path: '%s' filename: '%s'\n", inputPath.c_str(), xmlfile );
	string input = inputPath + xmlfile;

	TiXmlDocument xmlDoc;
	xmlDoc.LoadFile( input );
	if ( xmlDoc.Error() || !xmlDoc.FirstChildElement() ) {
		printf( "Failed to parse XML file. err=%s\n", xmlDoc.ErrorDesc() );
		exit( 2 );
	}

	xmlDoc.FirstChildElement()->QueryStringAttribute( "output", &outputPath );
	xmlDoc.FirstChildElement()->QueryStringAttribute( "outputDB", &outputDB );

	printf( "Output Path: %s\n", outputPath.c_str() );
	printf( "Output DataBase: %s\n", outputDB.c_str() );
	printf( "Processing tags:\n" );

	// Remove the old table.
	FILE* fp = fopen( outputDB.c_str(), "wb" );
	fclose( fp );

	int sqlResult = sqlite3_open( outputDB.c_str(), &db);
	GLASSERT( sqlResult == SQLITE_OK );
	writer = new BinaryDBWriter( db, true );
	CreateDatabase();

	for( TiXmlElement* child = xmlDoc.FirstChildElement()->FirstChildElement();
		 child;
		 child = child->NextSiblingElement() )
	{
		if (    child->ValueStr() == "texture" 
			 || child->ValueStr() == "image" ) 
		{
			ProcessTexture( child );
		}
		else if ( child->ValueStr() == "model" ) {
			ProcessModel( child );
		}
		else if ( child->ValueStr() == "map" ) {
			ProcessMap( child );
		}
		else {
			printf( "Unrecognized element: %s\n", child->Value() );
		}
	}

	int total = totalTextureMem + totalModelMem + totalMapMem;

	printf( "Total memory=%dk Texture=%dk Model=%dk Map=%dk\n", 
			total/1024, totalTextureMem/1024, totalModelMem/1024, totalMapMem/1024 );
	printf( "All done.\n" );
	SDL_Quit();

	delete writer;
	sqlResult = sqlite3_close( db );
	GLASSERT( sqlResult == SQLITE_OK );
	return 0;
}