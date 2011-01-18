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

#include "battlescene.h"
#include "characterscene.h"
#include "helpscene.h"
#include "dialogscene.h"

#include "game.h"
#include "cgame.h"

#include "../engine/uirendering.h"
#include "../engine/particle.h"
#include "../engine/text.h"

#include "battlestream.h"
#include "ai.h"
#include "tacticalendscene.h"

#include "../grinliz/glfixed.h"
#include "../micropather/micropather.h"
#include "../grinliz/glstringutil.h"
#include "../grinliz/glgeometry.h"

#include "../tinyxml/tinyxml.h"
#include "ufosound.h"
#include "settings.h"

using namespace grinliz;
using namespace gamui;

//#define REACTION_FIRE_EVENT_ONLY


BattleScene::BattleScene( Game* game ) : Scene( game )
{
	//GLRELASSERT( 0 );
	subTurnCount = 0;
	turnCount = 0;
	isDragging = false;

	engine  = game->engine;
	visibility.Init( this, units, engine->GetMap() );
	nearPathState.Clear();
	engine->GetMap()->SetPathBlocker( this );
	dragUnit = 0;

	aiArr[ALIEN_TEAM]		= new WarriorAI( ALIEN_TEAM, &visibility, engine, units );
	aiArr[TERRAN_TEAM]		= 0;
	if ( SettingsManager::Instance()->GetPlayerAI() ) {
		aiArr[TERRAN_TEAM] = new WarriorAI( TERRAN_TEAM, &visibility, engine, units );
	}
	aiArr[CIV_TEAM]			= new CivAI( CIV_TEAM, &visibility, engine, units );

	mapmaker_currentMapItem = 1;

	// On screen menu.
	const Screenport& port = engine->GetScreenport();

	const ButtonLook& green = game->GetButtonLook( Game::GREEN_BUTTON );
	const ButtonLook& blue = game->GetButtonLook( Game::BLUE_BUTTON );
	const ButtonLook& red = game->GetButtonLook( Game::RED_BUTTON );

	alienImage.Init( &gamui3D, UIRenderer::CalcDecoAtom( DECO_ALIEN ), true );
	alienImage.SetPos( float(port.UIWidth()-50), 0 );
	alienImage.SetSize( 50, 50 );

	nameRankUI.Init( &gamui3D );

	gamui::RenderAtom nullAtom;
	for( int i=0; i<MAX_UNITS; ++i ) {
		unitImage0[i].Init( &engine->GetMap()->overlay[Map::LAYER_UNDER_LOW], nullAtom, false );
		unitImage0[i].SetVisible( false );

		unitImage1[i].Init( &engine->GetMap()->overlay[Map::LAYER_UNDER_HIGH], nullAtom, false );
		unitImage1[i].SetVisible( false );
		unitImage1[i].SetForeground( true );
	}
	selectionImage.Init( &engine->GetMap()->overlay[Map::LAYER_OVER], UIRenderer::CalcIconAtom( ICON_STAND_HIGHLIGHT ), true );
	selectionImage.SetSize( 1, 1 );

	for( int i=0; i<MAX_ALIENS; ++i ) {
		targetArrow[i].Init( &gamui2D, UIRenderer::CalcIconAtom( ICON_TARGET_POINTER ), false );
		targetArrow[i].SetVisible( false );
	}

	for( int i=0; i<2; ++i ) {
		dragBar[i].Init( &engine->GetMap()->overlay[Map::LAYER_OVER], nullAtom, false );
		dragBar[i].SetLevel( -1 );
		dragBar[i].SetVisible( false );
	}

	const float SIZE = 50.0f;
	{
		exitButton.Init( &gamui2D, blue );
		exitButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_LAUNCH, true ), UIRenderer::CalcDecoAtom( DECO_LAUNCH, false ) );
		exitButton.SetSize( SIZE, SIZE );

		helpButton.Init( &gamui2D, green );
		helpButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_HELP, true ), UIRenderer::CalcDecoAtom( DECO_HELP, false ) );
		helpButton.SetSize( SIZE, SIZE );

		nextTurnButton.Init( &gamui2D, green );
		nextTurnButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_END_TURN, true ), UIRenderer::CalcDecoAtom( DECO_END_TURN, false ) );
		nextTurnButton.SetSize( SIZE, SIZE );

		targetButton.Init( &gamui2D, red );
		targetButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_AIMED, true ), UIRenderer::CalcDecoAtom( DECO_AIMED, false ) );
		targetButton.SetSize( SIZE, SIZE );

		invButton.Init( &gamui2D, green );
		invButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_CHARACTER, true ), UIRenderer::CalcDecoAtom( DECO_CHARACTER, false ) );
		invButton.SetSize( SIZE, SIZE );

		invButton.Init( &gamui2D, green );
		invButton.SetDeco( UIRenderer::CalcDecoAtom( DECO_CHARACTER, true ), UIRenderer::CalcDecoAtom( DECO_CHARACTER, false ) );
		invButton.SetSize( SIZE, SIZE );

		static const int controlDecoID[CONTROL_BUTTON_COUNT] = { DECO_ROTATE_CCW, DECO_ROTATE_CW, DECO_PREV, DECO_NEXT };
		for( int i=0; i<CONTROL_BUTTON_COUNT; ++i ) {
			controlButton[i].Init( (i==0) ? &gamui2D : &gamui3D, green );
			controlButton[i].SetDeco( UIRenderer::CalcDecoAtom( controlDecoID[i], true ), UIRenderer::CalcDecoAtom( controlDecoID[i], false ) );
			controlButton[i].SetSize( SIZE, SIZE );
		}

		UIItem* items[6] = { &exitButton, &helpButton, &nextTurnButton, &targetButton, &invButton, &controlButton[0] };
		Gamui::Layout( items, 6, 1, 6, 0, 0, SIZE, (float)port.UIHeight() );

		controlButton[1].SetPos( SIZE, port.UIHeight()-SIZE );
		controlButton[2].SetPos( port.UIWidth()-SIZE*2.f, port.UIHeight()-SIZE );
		controlButton[3].SetPos( port.UIWidth()-SIZE*1.f, port.UIHeight()-SIZE );

		RenderAtom menuImageAtom( (const void*)UIRenderer::RENDERSTATE_UI_NORMAL, (const void*)TextureManager::Instance()->GetTexture( "commandBarV" ), 0, 0, 1, 1, 50, 320 );
		menuImage.Init( &gamui2D, menuImageAtom, false );

		if ( Engine::mapMakerMode ) {
			for( int i=0; i<6; ++i ) {
				items[i]->SetVisible( false );
			}
			for( int i=0; i<4; ++i ) {
				controlButton[i].SetVisible( false );
			}
			menuImage.SetVisible( false );
		}
	}

	
	for( int i=0; i<3; ++i ) {
		fireButton[i].Init( &gamui3D, red );
		fireButton[i].SetSize( 120.f, 60.f );
		fireButton[i].SetDeco( UIRenderer::CalcIconAtom( ICON_GREEN_WALK_MARK, true ), UIRenderer::CalcIconAtom( ICON_GREEN_WALK_MARK, false ) );
		fireButton[i].SetVisible( false );
		fireButton[i].SetText( " " );
		fireButton[i].SetText2( " " );
		fireButton[i].SetDecoLayout( gamui::Button::RIGHT, 25, 0 );
		fireButton[i].SetTextLayout( gamui::Button::LEFT, 20, 0 );
	}	

	{
		static const float W = 0.15f;
		static const float H = 0.15f;
		static const float S = 0.02f;

		gamui::RenderAtom tick0Atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_GREEN, UIRenderer::PALETTE_GREEN, UIRenderer::PALETTE_BRIGHT, W, H );
		tick0Atom.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;
		gamui::RenderAtom tick1Atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_RED, UIRenderer::PALETTE_RED, 0, W, H );
		tick1Atom.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;
		gamui::RenderAtom tick2Atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_GREY, UIRenderer::PALETTE_GREY, UIRenderer::PALETTE_DARK, W, H );
		tick1Atom.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;

		for( int i=0; i<MAX_UNITS; ++i ) {
			hpBars[i].Init( &engine->GetMap()->overlay[Map::LAYER_UNDER_HIGH], 5, tick0Atom, tick1Atom, tick2Atom, S );
			hpBars[i].SetVisible( false );
		}
	}
	engine->EnableMap( true );

	if ( Engine::mapMakerMode )
	{
		const ModelResource* res = ModelResourceManager::Instance()->GetModelResource( "selection" );
		mapmaker_mapSelection = engine->AllocModel( res );
		mapmaker_mapSelection->SetPos( 0.5f, 0.0f, 0.5f );
		mapmaker_preview = 0;
	}

	currentTeamTurn = ALIEN_TEAM;
	NextTurn( false );
}


BattleScene::~BattleScene()
{
	engine->GetMap()->SetPathBlocker( 0 );
	ParticleSystem::Instance()->Clear();

	if ( Engine::mapMakerMode ) {
		if ( mapmaker_mapSelection ) 
			engine->FreeModel( mapmaker_mapSelection );
		if ( mapmaker_preview )
			engine->FreeModel( mapmaker_preview );
	}

	for( int i=0; i<3; ++i )
		delete aiArr[i];
}


void BattleScene::Activate()
{
	engine->EnableMap( true );
}


void BattleScene::DeActivate()
{
}

void BattleScene::NextTurn( bool saveOnTerranTurn )
{
	currentTeamTurn++;
	if ( currentTeamTurn == NUM_TEAMS )
		currentTeamTurn = 0;
	turnCount++;

	switch ( currentTeamTurn ) {
		case TERRAN_TEAM:
			GLOUTPUT(( "New Turn: Terran\n" ));
			for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i )
				units[i].NewTurn();
			currentUnitAI = TERRAN_UNITS_START;
			alienImage.SetVisible( false );
			break;

		case ALIEN_TEAM:
			GLOUTPUT(( "New Turn: Alien\n" ));
			for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
				units[i].NewTurn();
			}
			currentUnitAI = ALIEN_UNITS_START;

			alienImage.SetVisible( true );
			alienImage.SetAtom( UIRenderer::CalcDecoAtom( DECO_ALIEN ) );
			break;

		case CIV_TEAM:
			GLOUTPUT(( "New Turn: Civ\n" ));
			for( int i=CIV_UNITS_START; i<CIV_UNITS_END; ++i )
				units[i].NewTurn();
			currentUnitAI = CIV_UNITS_START;

			alienImage.SetVisible( true );
			alienImage.SetAtom( UIRenderer::CalcDecoAtom( DECO_HUMAN ) );
			break;

		default:
			GLRELASSERT( 0 );
			break;
	}

	// Allow the map to change (fire and smoke)
	Rectangle2I change;
	change.SetInvalid();
	engine->GetMap()->DoSubTurn( &change );
	visibility.InvalidateAll( change );

	// Since the map has changed:
	ProcessDoors();
	CalcTeamTargets();
	targetEvents.Clear();

	// Per turn save:
	if ( saveOnTerranTurn && currentTeamTurn == TERRAN_TEAM ) {
		game->Save();
	}

	if ( aiArr[currentTeamTurn] ) {
		aiArr[currentTeamTurn]->StartTurn( units );
	}
	else {
		if ( engine->GetMap()->GetLanderModel() )
			OrderNextPrev();
	}

}


void BattleScene::OrderNextPrev()
{
#ifdef SMART_ORDER
	const Model* lander = engine->GetMap()->GetLanderModel();
	GLRELASSERT( lander );

	Matrix2I mat, inv;
	mat.SetXZRotation( (int)lander->GetRotation() );
	mat.Invert( &inv );

	// Tricky problem. First sort into ordered by (x,z) buckets. Then
	// group "clumps" while keeping the bucket order.

	int	a_subTurnOrder[MAX_TERRANS];

	subTurnCount=0;
	for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		if ( units[i].IsAlive() ) {
			a_subTurnOrder[subTurnCount++] = i;
		}
	}
	if ( subTurnCount == 0 )
		return;

	// Yes, a bubble sort. But there are only 8 (possibly in the future
	// 16) soldiers to sort.
	//
	Vector2I v = { -1, MAP_SIZE };
	for( int j=0; j<(subTurnCount-1); ++j ) {
		Vector2I pos0 = inv * units[a_subTurnOrder[j]].Pos();
		int posScore0 = DotProduct( pos0, v );

		for( int i=j+1; i<subTurnCount; ++i ) {
			Vector2I posI = inv * units[a_subTurnOrder[i]].Pos();
			int posScoreI = DotProduct( posI, v );

			if ( posScoreI > posScore0 ) {
				Swap( &a_subTurnOrder[j], &a_subTurnOrder[i] );
			}
		}
	}


	Rectangle2I clump;
	const int FIRST_OUTSET = 2;
	const int ADD_OUTSET = 2;
	int count = 0;

	for( int j=0; j<subTurnCount; ++j ) {
		if ( a_subTurnOrder[j] >= 0 ) {
			subTurnOrder[count++] = a_subTurnOrder[j];

			clump.min = clump.max = units[ a_subTurnOrder[j] ].Pos();
			clump.Outset( FIRST_OUTSET );

			for( int i=j+1; i<subTurnCount; ++i ) {
				if ( a_subTurnOrder[i] >= 0 ) {
					if ( clump.Contains( units[ a_subTurnOrder[i] ].Pos() ) ) {
						subTurnOrder[count++] = a_subTurnOrder[i];

						Rectangle2I subClump;
						subClump.min = subClump.max = units[ a_subTurnOrder[i] ].Pos();
						a_subTurnOrder[i] = -1;

						subClump.Outset( ADD_OUTSET );
						clump.DoUnion( subClump );
					}
				}
			}
		}
	}
	GLRELASSERT( count == subTurnCount );
#else
	int count = 0;
	for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		if ( units[i].IsAlive() ) {
			subTurnOrder[count++] = i;
		}
	}
	subTurnCount = count;
#endif

}


void BattleScene::Save( FILE* fp, int depth )
{
	XMLUtil::OpenElement( fp, depth, "BattleScene" );
	XMLUtil::Attribute( fp, "currentTeamTurn", currentTeamTurn );
	XMLUtil::Attribute( fp, "dayTime", engine->GetMap()->DayTime() ? 1 : 0 );
	XMLUtil::Attribute( fp, "turnCount", turnCount );

	XMLUtil::SealCloseElement( fp );

	engine->GetMap()->Save( fp, depth );
	{
		XMLUtil::OpenElement( fp, depth, "Units" );
		XMLUtil::SealElement( fp );
	
		for( int i=0; i<MAX_UNITS; ++i ) {
			units[i].Save( fp, depth+1 );
		}
		XMLUtil::CloseElement( fp, depth, "Units" );
	}
}


void BattleScene::Load( const TiXmlElement* gameElement )
{
	// FIXME: Save/Load AI? Memory state is lost.

	selection.Clear();

	const TiXmlElement* battleElement = gameElement->FirstChildElement( "BattleScene" );
	if ( battleElement ) {
		battleElement->QueryIntAttribute( "currentTeamTurn", &currentTeamTurn );
		int daytime = 1;
		battleElement->QueryIntAttribute( "dayTime", &daytime );
		engine->GetMap()->SetDayTime( daytime ? true : false );

		turnCount = 0;
		battleElement->QueryIntAttribute( "turnCount", &turnCount );
	}

	engine->GetMap()->Clear();
	engine->GetMap()->Load( gameElement->FirstChildElement( "Map"), game->GetItemDefArr() );
	
	int team[3] = { TERRAN_UNITS_START, CIV_UNITS_START, ALIEN_UNITS_START };

	if ( gameElement->FirstChildElement( "Units" ) ) {
		for( const TiXmlElement* unitElement = gameElement->FirstChildElement( "Units" )->FirstChildElement( "Unit" );
			 unitElement;
			 unitElement = unitElement->NextSiblingElement( "Unit" ) ) 
		{
			int t = 0;
			unitElement->QueryIntAttribute( "team", &t );
			Unit* unit = &units[team[t]];
			unit->Load( unitElement, game );
			
			team[t]++;

			GLRELASSERT( team[0] <= TERRAN_UNITS_END );
			GLRELASSERT( team[1] <= CIV_UNITS_END );
			GLRELASSERT( team[2] <= ALIEN_UNITS_END );
		}
	}
	

	ProcessDoors();
	CalcTeamTargets();
	targetEvents.Clear();

	if ( aiArr[currentTeamTurn] ) {
		aiArr[currentTeamTurn]->StartTurn( units );
	}
	if ( engine->GetMap()->GetLanderModel() )
		OrderNextPrev();

//	if ( turnCount == 0 ) {
		//engine->GetMap()->SetLanderFlight( 1 );

		//Action* action = actionStack.Push();
		//action->Init( ACTION_LANDER, 0 );
		//action->type.lander.timeRemaining = LanderAction::TOTAL_TIME;

		//for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		//	if ( units[i].GetModel() ) {
		//		Model* m = units[i].GetModel();
		//		m->SetFlag( Model::MODEL_INVISIBLE );
		//	}
		//}
	//}

	for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		if ( units[i].IsAlive() && units[i].GetModel() ) {
			engine->camera.SetPosWC( -12.f, 45.f, 52.f );	// standard test
			PushScrollOnScreen( units[i].GetModel()->Pos(), true );
			break;
		}
	}
}


void BattleScene::SetFogOfWar()
{
	//GRINLIZ_PERFTRACK

	if ( visibility.FogCheckAndClear() ) {
		grinliz::BitArray<Map::SIZE, Map::SIZE, 1>* fow = engine->GetMap()->LockFogOfWar();
		for( int j=0; j<MAP_SIZE; ++j ) {
			for( int i=0; i<MAP_SIZE; ++i ) {
				if ( visibility.TeamCanSee( TERRAN_TEAM, i, j ) )
					fow->Set( i, j );
				else
					fow->Clear( i, j );
			}
		}

		// Can always see around the lander.		
		const Model* landerModel = engine->GetMap()->GetLanderModel();
		if ( landerModel ) {
			Rectangle2I bounds;
			engine->GetMap()->MapBoundsOfModel( landerModel, &bounds );
			fow->SetRect( bounds );
		}
		engine->GetMap()->ReleaseFogOfWar();
	}
}


void BattleScene::TestHitTesting()
{
	/*
	GRINLIZ_PERFTRACK
	for ( int i=0; i<MAX_UNITS; ++i ) {
		if ( units[i].Status() == Unit::STATUS_ALIVE ) {
			int cx = (int)units[i].GetModel()->X();
			int cz = (int)units[i].GetModel()->Z();
			const int DELTA = 10;

			Ray ray;
			ray.origin.Set( (float)cx+0.5f, 1.0f, (float)cz+0.5f );

			for( int x=cx-DELTA; x<cx+DELTA; ++x ) {
				for( int z=cz-DELTA; z<cz+DELTA; ++z ) {
					if ( x>=0 && z>=0 && x<engine->GetMap()->Width() && z<engine->GetMap()->Height() ) {

						Vector3F dest = {(float)x, 1.0f, (float)z};

						ray.direction = dest - ray.origin;
						ray.length = 1.0f;

						Vector3F intersection;
						engine->IntersectModel( ray, TEST_TRI, 0, 0, &intersection );
					}
				}
			}
		}
	}
	*/
}


int BattleScene::CenterRectIntersection(	const Vector2F& r,
											const Rectangle2F& rect,
											Vector2F* out )
{
	Vector2F center = { (rect.min.x+rect.max.x)*0.5f, (rect.min.y+rect.max.y)*0.5f };
	Vector2F rf = { r.x, r.y };
	Vector2F outf;

	for( int e=0; e<4; ++e ) {
		Vector2F p0, p1;
		rect.Edge( e, &p1, &p0 );
		Vector2F p0f = { p0.x, p0.y };
		Vector2F p1f = { p1.x, p1.y };

		float t0, t1;
		int result = IntersectLineLine( center, rf, 
										p0f, p1f, 
										&outf, &t0, &t1 );
		if (    result == grinliz::INTERSECT
			 && t0 >= 0 && t0 <= 1 && t1 >= 0 && t1 <= 1 ) 
		{
			out->Set( outf.x, outf.y );
			return grinliz::INTERSECT;
		}
	}
	GLASSERT( 0 );
	return grinliz::REJECT;
}


int BattleScene::RenderPass( grinliz::Rectangle2I* clip3D, grinliz::Rectangle2I* clip2D )
{
	if ( Engine::mapMakerMode ) {
		clip3D->SetInvalid();
		clip2D->SetInvalid();
		return RENDER_3D | RENDER_2D; 
	}
	else {
		Vector2F size;
		size.x = menuImage.Width();
		size.y = menuImage.Height();
		const Screenport& port = engine->GetScreenport();

		clip3D->Set( (int)size.x, 0, (int)port.UIWidth(), (int)port.UIHeight() );
		clip2D->Set(0, 0, (int)port.UIWidth(), (int)port.UIHeight() );
		return RENDER_3D | RENDER_2D; 
	}
}


void BattleScene::SetUnitOverlays()
{
	gamui::RenderAtom targetAtom0 = UIRenderer::CalcIconAtom( ICON_TARGET_STAND );
	gamui::RenderAtom targetAtom1 = targetAtom0;
	targetAtom0.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;

	gamui::RenderAtom greenAtom0 = UIRenderer::CalcIconAtom( ICON_GREEN_STAND_MARK );
	gamui::RenderAtom yellowAtom0 = UIRenderer::CalcIconAtom( ICON_YELLOW_STAND_MARK );
	gamui::RenderAtom orangeAtom0 = UIRenderer::CalcIconAtom( ICON_ORANGE_STAND_MARK );

	gamui::RenderAtom greenAtom1 = UIRenderer::CalcIconAtom( ICON_GREEN_STAND_MARK_OUTLINE );
	gamui::RenderAtom yellowAtom1 = UIRenderer::CalcIconAtom( ICON_YELLOW_STAND_MARK_OUTLINE );
	gamui::RenderAtom orangeAtom1 = UIRenderer::CalcIconAtom( ICON_ORANGE_STAND_MARK_OUTLINE );

	greenAtom0.renderState = (const void*)Map::RENDERSTATE_MAP_TRANSLUCENT;
	yellowAtom0.renderState = (const void*)Map::RENDERSTATE_MAP_TRANSLUCENT;
	orangeAtom0.renderState = (const void*)Map::RENDERSTATE_MAP_TRANSLUCENT;
	greenAtom1.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;
	yellowAtom1.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;
	orangeAtom1.renderState = (const void*)Map::RENDERSTATE_MAP_NORMAL;

	const Unit* unitMoving = 0;
	if ( !actionStack.Empty() ) {
		if ( actionStack.Top()->actionID == ACTION_MOVE ) {
			unitMoving = actionStack.Top()->unit;
		}
	}
	
	static const float HP_DX = 0.10f;
	static const float HP_DY = 0.95f;

	for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
		// layer 0 target arrow
		// layer 1 target arrow

		if ( unitMoving != &units[i] && units[i].IsAlive() && visibility.TeamCanSee( TERRAN_TEAM, units[i].Pos() ) ) {
			Vector3F p;
			units[i].CalcPos( &p );

			// Is the unit on screen? If so, put in a simple foot decal. Else
			// put in an "alien that way" decal.
			Vector2F ui;	
			engine->GetScreenport().WorldToUI( p, &ui );
			const Rectangle2F& uiBounds = engine->GetScreenport().UIBoundsClipped3D();

			if ( uiBounds.Contains( ui ) ) {
				unitImage0[i].SetVisible( true );
				unitImage0[i].SetAtom( targetAtom0 );
				unitImage0[i].SetSize( 1.2f, 1.2f );
				unitImage0[i].SetCenterPos( p.x, p.z );

				unitImage1[i].SetVisible( true );
				unitImage1[i].SetAtom( targetAtom1 );
				unitImage1[i].SetSize( 1.2f, 1.2f );
				unitImage1[i].SetCenterPos( p.x, p.z );

				hpBars[i].SetVisible( true );
				hpBars[i].SetPos( p.x + HP_DX - 0.5f, p.z + HP_DY - 0.5f );
				hpBars[i].SetRange( (float)units[i].HP()*0.01f, (float)units[i].GetStats().TotalHP()*0.01f );

				targetArrow[i-ALIEN_UNITS_START].SetVisible( false );
			}
			else {
				unitImage0[i].SetVisible( false );
				unitImage1[i].SetVisible( false );
				hpBars[i].SetVisible( false );

				targetArrow[i-ALIEN_UNITS_START].SetVisible( true );

				Vector2F center = { (uiBounds.min.x + uiBounds.max.x)/2,
									(uiBounds.min.y + uiBounds.max.y)/2 };
				Rectangle2F inset = uiBounds;
				const float EPS = 10;
				inset.Outset( -EPS );
				Vector2F intersection = { 0, 0 };
				CenterRectIntersection( ui, inset, &intersection );

				targetArrow[i-ALIEN_UNITS_START].SetCenterPos( intersection.x, intersection.y );
				float angle = atan2( (ui.y-center.y), (ui.x-center.x) );
				angle = ToDegree( angle )+ 90.0f;	

				targetArrow[i-ALIEN_UNITS_START].SetRotationZ( angle );
				targetArrow[i-ALIEN_UNITS_START].SetVisible( true );
				targetArrow[i-ALIEN_UNITS_START].SetSize( 50, 50 );
			}
		}
		else {
			targetArrow[i-ALIEN_UNITS_START].SetVisible( false );
			unitImage0[i].SetVisible( false );
			unitImage1[i].SetVisible( false );
			hpBars[i].SetVisible( false );
			targetArrow[i-ALIEN_UNITS_START].SetVisible( false );
		}
	}

	for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		if ( unitMoving != &units[i] && units[i].IsAlive() ) {
			int remain = units[i].CalcWeaponTURemaining( 0 );
			Vector2I pos = units[i].Pos();

			if ( remain >= Unit::AUTO_SHOT ) {
				unitImage0[i].SetAtom( greenAtom0 );
				unitImage1[i].SetAtom( greenAtom1 );
			}
			else if ( remain == Unit::SNAP_SHOT ) {
				unitImage0[i].SetAtom( yellowAtom0 );
				unitImage1[i].SetAtom( yellowAtom1 );
			}
			else {
				unitImage0[i].SetAtom( orangeAtom0 );
				unitImage1[i].SetAtom( orangeAtom1 );
			}

			unitImage0[i].SetVisible( true );
			unitImage0[i].SetPos( (float)pos.x, (float)pos.y );
			unitImage0[i].SetSize( 1, 1 );

			unitImage1[i].SetVisible( true );
			unitImage1[i].SetPos( (float)pos.x, (float)pos.y );
			unitImage1[i].SetSize( 1, 1 );

			hpBars[i].SetVisible( true );
			hpBars[i].SetPos( (float)pos.x + HP_DX, (float)pos.y + HP_DY );
			hpBars[i].SetVisible( true );
			hpBars[i].SetRange( (float)units[i].HP()*0.01f, (float)units[i].GetStats().TotalHP()*0.01f );
		}
		else {
			unitImage0[i].SetVisible( false );
			unitImage1[i].SetVisible( false );
			hpBars[i].SetVisible( false );
		}
	}

	if ( SelectedSoldierUnit() && unitMoving != SelectedSoldierUnit() ) {
		Vector2I pos = SelectedSoldierUnit()->Pos();
		selectionImage.SetPos( (float)pos.x, (float)pos.y );
		selectionImage.SetVisible( true );
	}
	else {
		selectionImage.SetVisible( false );
	}
}


void BattleScene::DoTick( U32 currentTime, U32 deltaTime )
{
	GRINLIZ_PERFTRACK
	TestHitTesting();
	engine->GetMap()->EmitParticles( deltaTime );

#if 0
	// Test particle system.
	{
		ParticleSystem* system = ParticleSystem::Instance();
		Vector3F pos = { (float)(engine->GetMap()->Width() - 2), 0, (float)(engine->GetMap()->Height() - 2) };
		
		system->EmitFlame( deltaTime, pos );
		pos.z -= 1.0f;

		Color4F c = { 1, 1, 0, 1 };
		Color4F cVel = { -0.2f, -0.2f, 0.f, -0.2f };
		Vector3F vel = { 0, 2, 0 };

		system->EmitPoint( 3, ParticleSystem::PARTICLE_RAY, c, cVel, pos, 0.1f, vel, 0.1f, 5000 );
	}
#endif
	

#if 0
		// Debug unit targets.
		for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
			if ( targets.terran.alienTargets.IsSet( unitID-TERRAN_UNITS_START, i-ALIEN_UNITS_START ) ) {
				Vector3F p;
				units[i].CalcPos( &p );
				game->particleSystem->EmitDecal( ParticleSystem::DECAL_UNIT_SIGHT,
												 ParticleSystem::DECAL_BOTH,
												 p, ALPHA, 0 );	
			}
		}
		// Debug team targets.
		for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
			if ( targets.terran.teamAlienTargets.IsSet( i-ALIEN_UNITS_START ) ) {
				Vector3F p;
				units[i].CalcPos( &p );
				game->particleSystem->EmitDecal( ParticleSystem::DECAL_TEAM_SIGHT,
												 ParticleSystem::DECAL_BOTH,
												 p, ALPHA, 0 );	
			}
		}
#endif


	int result = ProcessAction( deltaTime );
	SetFogOfWar();	// fast if nothing changed.	

	if ( result & STEP_COMPLETE ) {
		ProcessDoors();			// Not fast. Only calc when needed.
		CalcTeamTargets();

		DumpTargetEvents();

		StopForNewTeamTarget();
		DoReactionFire();
		targetEvents.Clear();	// All done! They don't get to carry on beyond the moment.
	}

	SetUnitOverlays();

	if ( result && EndCondition( &tacticalData ) ) {
		game->PushScene( Game::END_SCENE, new TacticalEndSceneData( tacticalData ) );
	}
	else { 
		if ( currentTeamTurn == TERRAN_TEAM ) {
			if ( selection.soldierUnit && !selection.soldierUnit->IsAlive() ) {
				SetSelection( 0 );
			}
			if ( actionStack.Empty() ) {
				ShowNearPath( selection.soldierUnit );	// fast if it does nothing.
			}
			// Render the target (if it is on-screen)
			if ( HasTarget() ) {
				DrawFireWidget();
			}
			else {
				fireButton[0].SetVisible( false );
				fireButton[1].SetVisible( false );
				fireButton[2].SetVisible( false );
			}
		}
		if ( actionStack.Empty() ) {
			if ( aiArr[currentTeamTurn] ) {
				bool done = ProcessAI();
				if ( done ) {
					NextTurn( true );
				}
			}
		}
	}
}


void BattleScene::Debug3D()
{
	/*
	int start[2] = { TERRAN_UNITS_START, ALIEN_UNITS_START };
	int end[2]   = { TERRAN_UNITS_END, ALIEN_UNITS_END };

	for( int k=0; k<2; ++k ) {
		if ( units[start[k]].IsAlive() ) {
			Vector3F origin;
			units[start[k]].GetModel()->CalcTrigger( &origin );
			const Model* ignore[] = { units[start[k]].GetModel(), units[start[k]].GetWeaponModel(), 0 };

			for( int i=start[k]+1; i<end[k]; ++i ) {
				if ( !units[i].IsAlive() )
					continue;

				Vector3F target;
				units[i].GetModel()->CalcTarget( &target );

				Ray ray;
				ray.direction = target - origin;
				ray.origin = origin;

				Vector3F intersect;
				engine->IntersectModel( ray, TEST_TRI, 0, 0, ignore, &intersect );

				glDisableClientState( GL_NORMAL_ARRAY );
				glDisableClientState( GL_VERTEX_ARRAY );
				glDisableClientState( GL_TEXTURE_COORD_ARRAY );
				glDisable( GL_TEXTURE_2D );

				glLineWidth( 2.0f );

				glColor4f( 0, 1, 0, 1 );
				glBegin( GL_LINES );
				glVertex3f( origin.x, origin.y, origin.z );
				glVertex3f( intersect.x, intersect.y, intersect.z );
				glEnd();

				glColor4f( 1, 0, 0, 1 );
				glBegin( GL_LINES );
				glVertex3f( intersect.x, intersect.y, intersect.z );
				glVertex3f( target.x, target.y, target.z );
				glEnd();

				glEnable( GL_TEXTURE_2D );
				glEnableClientState( GL_NORMAL_ARRAY );
				glEnableClientState( GL_VERTEX_ARRAY );
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );

			}
		}
	}
	*/
}


bool BattleScene::EndCondition( TacticalEndSceneData* data )
{
	memset( data, 0, sizeof( *data ) );
	data->units = units;

	for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
		if ( units[i].InUse() )
			++data->nTerrans;
		if ( units[i].IsAlive() )
			++data->nTerransAlive;
	}
	for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
		if ( units[i].InUse() )
			++data->nAliens;
		if ( units[i].IsAlive() )
			++data->nAliensAlive;
	}
	for( int i=CIV_UNITS_START; i<CIV_UNITS_END; ++i ) {
		if ( units[i].InUse() )
			++data->nCivs;
		if ( units[i].IsAlive() )
			++data->nCivsAlive;
	}

	// If the terrans are all down for the count, then it acts like the 
	// lander leaving. KO becomes MIA.
	if ( data->nTerransAlive == 0 ) {
		for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
			if ( units[i].InUse() )
				units[i].Leave();
		}
	}

	if ( data->nAliensAlive == 0 ||data->nTerransAlive == 0 )
		return true;
	return false;
}



void BattleScene::DrawFireWidget()
{
	GLRELASSERT( HasTarget() );
	GLRELASSERT( SelectedSoldierUnit() );

	Unit* unit = SelectedSoldierUnit();
	
	Vector3F target;
	BulletTarget bulletTarget;

	if ( selection.targetPos.x >= 0 ) {
		target.Set( (float)selection.targetPos.x+0.5f, 1.0f, (float)selection.targetPos.y+0.5f );
	}
	else {
		Model* m = selection.targetUnit->GetModel();
		GLASSERT( m );
		m->CalcTarget( &target );
		m->CalcTargetSize( &bulletTarget.width, &bulletTarget.height );
	}

	Vector3F distVector = target - SelectedSoldierModel()->Pos();
	bulletTarget.distance = distVector.Length();

	Inventory* inventory = unit->GetInventory();
	const WeaponItemDef* wid = unit->GetWeaponDef();

	float snapTU=0, autoTU=0, altTU=0;
	if ( wid ) 
		unit->AllFireTimeUnits( &snapTU, &autoTU, &altTU );

	for( int i=0; i<3; ++i ) {
		if ( wid && wid->HasWeapon( (WeaponMode)i ) )
		{
			float fraction, anyFraction, dptu, tu;

			unit->FireStatistics( (WeaponMode)i, bulletTarget, &fraction, &anyFraction, &tu, &dptu );
			int nRounds = inventory->CalcClipRoundsTotal( wid->GetClipItemDef( (WeaponMode)i) );

			anyFraction = Clamp( anyFraction, 0.0f, 0.95f );

			char buffer0[32];
			char buffer1[32];
			SNPrintf( buffer0, 32, "%s %d%%", wid->fireDesc[i], (int)LRintf( anyFraction*100.0f ) );
			SNPrintf( buffer1, 32, "%d/%d", wid->RoundsNeeded( (WeaponMode)i ), nRounds );

			fireButton[i].SetEnabled( true );
			fireButton[i].SetText( buffer0 );
			fireButton[i].SetText2( buffer1 );

			if ( unit->CanFire( (WeaponMode) i )) {
				// Reflect the TU left.
				float tuAfter = unit->TU() - tu;
				int tuIndicator = ICON_ORANGE_WALK_MARK;

				if ( tuAfter >= autoTU ) {
					tuIndicator = ICON_GREEN_WALK_MARK;
				}
				else if ( tuAfter >= snapTU ) {
					tuIndicator = ICON_YELLOW_WALK_MARK;
				}
				else if ( tuAfter < snapTU ) {
					tuIndicator = ICON_ORANGE_WALK_MARK;
				}
				fireButton[i].SetDeco( UIRenderer::CalcIconAtom( tuIndicator, true ), UIRenderer::CalcIconAtom( tuIndicator, false ) );
			}
			else {
				fireButton[i].SetEnabled( false );
				RenderAtom nullAtom;
				fireButton[i].SetDeco( nullAtom, nullAtom );
			}
		}				 
		else {			 
			fireButton[i].SetEnabled( false );
			fireButton[i].SetText( "[none]" );
			fireButton[i].SetText2( "" );
			RenderAtom nullAtom;
			fireButton[i].SetDeco( nullAtom, nullAtom );
		}
	}

	Vector2F view, ui;
	const Screenport& port = engine->GetScreenport();
	port.WorldToView( target, &view );
	port.ViewToUI( view, &ui );

	const int DX = 10;

	// Make sure it fits on the screen.
	float w = fireButton[0].Width();
	float h = fireButton[0].Height()*3.f + (float)FIRE_BUTTON_SPACING*2.0f;
	float x = (float)(ui.x+DX);
	float y = (float)(ui.y) - h*0.5f;

	if ( x < 0 ) {
		x = 0;
	}
	else if ( x+w >= port.UIWidth() ) {
		x = port.UIWidth() - w;
	}
	if ( y < 0 ) {
		y = 0;
	}
	else if ( y+h >= port.UIHeight() ) {
		y = port.UIHeight() - h;
	}
	fireButton[0].SetPos( x, y );
	fireButton[1].SetPos( x, y+fireButton[0].Height()+(float)FIRE_BUTTON_SPACING );
	fireButton[2].SetPos( x, y+fireButton[0].Height()*2.0f+(float)FIRE_BUTTON_SPACING*2.0f );
	fireButton[0].SetVisible( true );
	fireButton[1].SetVisible( true );
	fireButton[2].SetVisible( true );
}


void BattleScene::TestCoordinates()
{
	//const Screenport& port = engine->GetScreenport();
	Rectangle2F uiBounds = engine->GetScreenport().UIBoundsClipped3D();
	Rectangle2F inset = uiBounds;
	inset.Outset( 0 );

	Matrix4 mvpi;
	engine->GetScreenport().ViewProjectionInverse3D( &mvpi );

	for( int i=0; i<4; ++i ) {
		Vector3F pos;
		Vector2F corner;
		uiBounds.Corner( i, &corner );
		if ( engine->RayFromViewToYPlane( corner, mvpi, 0, &pos ) ) {
			Color4F c = { 0, 1, 1, 1 };
			Color4F cv = { 0, 0, 0, 0 };
			ParticleSystem::Instance()->EmitOnePoint( c, cv, pos );
		}
	}
}


grinliz::Rectangle2F BattleScene::CalcInsetUIBounds()
{
	// This should work, but isn't initalized when PushScrollOnScreen
	// gets called the first time.
	//	Rectangle2F uiBounds = port.UIBoundsClipped3D();
	Rectangle2I clip3D, clip2D;
	RenderPass( &clip3D, &clip2D );
	Rectangle2F uiBounds;
	uiBounds.Set(	(float)clip3D.min.x, (float)clip3D.min.y, 
					(float)clip3D.max.x, (float)clip3D.max.y );
	
	Rectangle2F inset = uiBounds;
	inset.Outset( -20 );
	return inset;
}



void BattleScene::PushScrollOnScreen( const Vector3F& pos, bool center )
{
	/*
		Centering is straightforward but not a great experience. Really want to scroll
		in to the nearest point. Do this by computing the desired point, offset it back,
		and add the difference.
	*/
	const Screenport& port = engine->GetScreenport();

	Vector2F view, ui;
	port.WorldToView( pos, &view );
	port.ViewToUI( view, &ui );

	Rectangle2F inset = CalcInsetUIBounds();

	if ( !inset.Contains( ui ) ) {
		Vector3F c;
		engine->CameraLookingAt( &c );

		Vector3F delta = pos - c;
		float len = delta.Length();
		delta.Normalize();

		Action* action = actionStack.Push();
		action->Init( ACTION_CAMERA_BOUNDS, 0 );
		action->type.cameraBounds.target = pos;
		action->type.cameraBounds.normal = delta;
		action->type.cameraBounds.speed = (float)MAP_SIZE / 3.0f;
		action->type.cameraBounds.center = center;
	}
}


void BattleScene::SetSelection( Unit* unit ) 
{
	if ( !unit ) {
		selection.soldierUnit = 0;
		selection.targetUnit = 0;
	}
	else {
		GLRELASSERT( unit->IsAlive() );

		if ( unit->Team() == TERRAN_TEAM ) {
			selection.soldierUnit = unit;
			selection.targetUnit = 0;
		}
		else if ( unit->Team() == ALIEN_TEAM ) {
			GLRELASSERT( SelectedSoldier() );
			selection.targetUnit = unit;
			selection.targetPos.Set( -1, -1 );
		}
		else {
			GLRELASSERT( 0 );
		}
	}
}


void BattleScene::PushRotateAction( Unit* src, const Vector3F& dst3F, bool quantize )
{
	GLRELASSERT( src->GetModel() );

	Vector2I dst = { (int)dst3F.x, (int)dst3F.z };

	float rot = src->AngleBetween( dst, quantize );
	if ( src->GetModel()->GetRotation() != rot ) {
		Action* action = actionStack.Push();
		action->Init( ACTION_ROTATE, src );
		action->type.rotate.rotation = rot;
	}
}


bool BattleScene::PushShootAction( Unit* unit, 
								   const grinliz::Vector3F& target, 
								   float targetWidth, float targetHeight,
								   WeaponMode mode,
								   float useError,
								   bool clearMoveIfShoot )
{
	GLRELASSERT( unit );

	if ( !unit->IsAlive() )
		return false;
	
	Item* weapon = unit->GetWeapon();
	if ( !weapon )
		return false;

	const WeaponItemDef* wid = weapon->IsWeapon();

	Vector3F normal, right, up, p;
	unit->CalcPos( &p );
	normal = target - p;
	float length = normal.Length();
	normal.Normalize();

	up.Set( 0.0f, 1.0f, 0.0f );
	CrossProduct( normal, up, &right );
	CrossProduct( normal, right, &up );

	BulletTarget bulletTarget( length );
	if ( targetWidth )
		bulletTarget.width = targetWidth;
	if ( targetHeight )
		bulletTarget.height = targetHeight;

	if ( unit->CanFire( mode ) )
	{
		int nShots = wid->RoundsNeeded( mode );
		unit->UseTU( unit->FireTimeUnits( mode ) );

		// Could be removed, if optimization needed. Computes the metrics.
		float chanceToHit, chanceAnyHit, tu, dptu;
		unit->FireStatistics( mode, bulletTarget, &chanceToHit, &chanceAnyHit, &tu, &dptu );

		for( int i=0; i<nShots; ++i ) {
			Vector3F t = target;
			if ( useError ) {
				BulletSpread bulletSpread;
				bulletSpread.Generate( random.Rand(), 
									   unit->CalcAccuracy( mode ), length,
									   normal, target, &t );
			}

			if ( clearMoveIfShoot && !actionStack.Empty() && actionStack.Top()->actionID == ACTION_MOVE ) {
				actionStack.Clear();
			}

			Action* action = actionStack.Push();
			action->Init( ACTION_SHOOT, unit );
			action->type.shoot.target = t;
			action->type.shoot.mode = mode;
			action->type.shoot.chanceToHit = chanceToHit;
			GLASSERT( InRange( chanceToHit, 0.0f, 1.0f ) );
			unit->GetInventory()->UseClipRound( wid->GetClipItemDef( mode ) );
		}
		PushRotateAction( unit, target, false );
		return true;
	}
	return false;
}


void BattleScene::DoReactionFire()
{
	if ( currentTeamTurn == CIV_TEAM )
		return;

	int antiTeam = ALIEN_TEAM;
	if ( currentTeamTurn == ALIEN_TEAM )
		antiTeam = TERRAN_TEAM;

	bool react = false;
	if ( actionStack.Empty() ) {
		react = true;
	}
	else { 
		const Action& action = *actionStack.Top();
		if (    action.actionID == ACTION_MOVE
			&& action.unit
			&& action.unit->Team() == currentTeamTurn
			&& action.type.move.pathFraction == 0 )
		{
			react = true;		
		}
	}
	if ( react ) {
		int i=0;
		while( i < targetEvents.Size() ) {
			TargetEvent t = targetEvents[i];

			// Reaction fire occurs on the *antiTeam*. It's a little
			// strange to get the ol' head wrapped around.
			if (    t.team == 0				// individual
				 && units[t.viewerID].Team() == antiTeam
				 && units[t.targetID].Team() == currentTeamTurn ) 
			{
				// Reaction fire
				Unit* targetUnit = &units[t.targetID];
				Unit* srcUnit = &units[t.viewerID];

				if (    targetUnit->IsAlive() 
					 && targetUnit->GetModel()
					 && srcUnit->GetWeapon() ) {

					bool rangeOK = true;
					// Filter out explosive weapons...
					if ( srcUnit->GetWeapon()->IsWeapon()->IsExplosive( kSnapFireMode ) ) {
						Vector2I d = srcUnit->Pos() - targetUnit->Pos();
						if ( d.LengthSquared() < EXPLOSIVE_RANGE * EXPLOSIVE_RANGE )
							rangeOK = false;
					}
					
					if ( rangeOK ) {
						// Do we really react? Are we that lucky? Well, are you, punk?
						float r = random.Uniform();
						float reaction = srcUnit->GetStats().Reaction();

						// Reaction is impacted by rotation.
						// Multiple ways to do the math. Go with the normal of the src facing to
						// the normal of the target.

						Vector2I targetMapPos = targetUnit->Pos();
						Vector2I srcMapPos = srcUnit->Pos();

						Vector2F normalToTarget = { (float)(targetMapPos.x - srcMapPos.x), (float)(targetMapPos.y - srcMapPos.y) };
						normalToTarget.Normalize();

						Vector2F facing;
						facing.x = sinf( ToRadian( srcUnit->GetModel()->GetRotation() ) );
						facing.y = cosf( ToRadian( srcUnit->GetModel()->GetRotation() ) );

						float mod = DotProduct( facing, normalToTarget ) * 0.5f + 0.5f;  
						reaction *= mod;				// linear with angle.
						float error = 2.0f - mod;		// doubles with rotation
						
						GLOUTPUT(( "reaction fire possible. (if %.2f < %.2f)\n", r, reaction ));

						if ( r <= reaction ) {
							Vector3F target;
							targetUnit->GetModel()->CalcTarget( &target );

							float targetWidth, targetHeight;
							targetUnit->GetModel()->CalcTargetSize( &targetWidth, &targetHeight );
							
							int shot = PushShootAction( srcUnit, target, targetWidth, targetHeight, kAutoFireMode, error, true );	// auto
							if ( !shot )
								PushShootAction( srcUnit, target, targetWidth, targetHeight, kSnapFireMode, error, true );	// snap
						}
				}
				}
				targetEvents.SwapRemove( i );
			}
			else {
				++i;
			}
		}
	}
}


void BattleScene::ProcessInventoryAI( Unit* theUnit )
{
	AI_LOG(( "[ai] Unit %d INVENTORY: ", currentUnitAI ));
	// Drop all the weapons and clips. Pick up new weapons and clips.
	Vector2I pos = theUnit->Pos();
	Storage* storage = engine->GetMap()->LockStorage( pos.x, pos.y, game->GetItemDefArr() );
	GLRELASSERT( storage );

	Inventory* inventory = theUnit->GetInventory();
	Item item;

	if ( storage ) {
		// Exception version=490 device=win32 at 	Sat, November 27, 2010 6:25 pm
		// The IsResupply() is used by the AI query, an the same thing needs to 
		// be used here, else we can infinte loop.
		const WeaponItemDef* wid = storage->IsResupply( theUnit->GetWeaponDef() );
		if ( wid ) {

			// Clear out everything actually being carried so we don't run out of space.
			for( int i=0; i<Inventory::NUM_SLOTS; ++i ) {
				item = inventory->GetItem( i );
				if ( item.IsWeapon() || item.IsClip() ) {
					storage->AddItem( item );
					inventory->RemoveItem( i );
				}
			}

			storage->RemoveItem( wid, &item );
			int slot = inventory->AddItem( item );
			GLRELASSERT( slot == Inventory::WEAPON_SLOT );
			AI_LOG(( "'%s' ", item.Name() ));

			// clips.
			WeaponMode mode[2] = { kSnapFireMode, kAltFireMode };
			for( int k=0; k<2; ++k ) {
				while ( storage->GetCount( wid->GetClipItemDef( mode[k] ) ) ) {
					Item item;
					storage->RemoveItem( wid->GetClipItemDef( mode[k] ), &item );
					if ( inventory->AddItem( item ) < 0 ) {
						storage->AddItem( item );
						break;
					}
					else {
						AI_LOG(( "'%s' ", item.Name() ));
					}
				}
			}
			AI_LOG(( "\n" ));
		}
	}
	engine->GetMap()->ReleaseStorage( storage );
	theUnit->UpdateInventory();
}


bool BattleScene::ProcessAI()
{
	GLRELASSERT( actionStack.Empty() );
	GLASSERT( aiArr[currentTeamTurn] );

	int count = 0;

	while ( actionStack.Empty() ) {

		if ( currentUnitAI == MAX_UNITS || units[currentUnitAI].Team() != currentTeamTurn )
			return true;	// indexed out of the correct team.

		if ( !units[currentUnitAI].IsAlive() ) {
			++currentUnitAI;
			continue;
		}

		int flags = units[currentUnitAI].AI();
		AI::AIAction aiAction;

		bool done = aiArr[currentTeamTurn]->Think( &units[currentUnitAI], flags, engine->GetMap(), &aiAction );

		switch ( aiAction.actionID ) {
			case AI::ACTION_SHOOT:
				{
					AI_LOG(( "[ai] Unit %d SHOOT\n", currentUnitAI ));
					bool shot = PushShootAction( &units[currentUnitAI], 
												 aiAction.shoot.target, 
												 aiAction.shoot.targetWidth, aiAction.shoot.targetHeight, 
												 aiAction.shoot.mode, 1.0f, false );
					GLRELASSERT( shot );
					if ( !shot )
						done = true;
				}
				break;

			case AI::ACTION_MOVE:
				{
					AI_LOG(( "[ai] Unit %d MOVE pathlen=%d\n", currentUnitAI, aiAction.move.path.pathLen ));
					Action* action = actionStack.Push();
					action->Init( ACTION_MOVE, &units[currentUnitAI] );
					action->type.move.path = aiAction.move.path;
				}
				break;

			case AI::ACTION_ROTATE:
				{
					AI_LOG(( "[ai] Unit %d ROTATE\n", currentUnitAI ));
					Vector3F target = { (float)aiAction.rotate.x, 0, (float)aiAction.rotate.y};
					PushRotateAction( &units[currentUnitAI], target, true );
				}
				break;

			case AI::ACTION_INVENTORY:
				ProcessInventoryAI( &units[currentUnitAI] );
				break;

			case AI::ACTION_NONE:
				break;

			default:
				GLRELASSERT( 0 );
				break;
		}
		if ( done ) {
			currentUnitAI++;
		}
	}
	return false;
}


void BattleScene::ProcessDoors()
{
	Vector2I loc[MAX_UNITS];
	int nLoc = 0;

	for( int i=0; i<MAX_UNITS; ++i ) {
		if ( units[i].IsAlive() )
			loc[nLoc++] = units[i].Pos();
	}
	if ( engine->GetMap()->ProcessDoors( loc, nLoc ) ) {
		visibility.InvalidateAll();
	}
}


void BattleScene::StopForNewTeamTarget()
{
	if ( currentTeamTurn == CIV_TEAM )
		return;

	int antiTeam = ALIEN_TEAM;
	if ( currentTeamTurn == ALIEN_TEAM )
		antiTeam = TERRAN_TEAM;

	if ( actionStack.Size() == 1 ) {
		const Action& action = *actionStack.Top();
		if (   action.actionID == ACTION_MOVE
			&& action.unit
			&& action.unit->Team() == currentTeamTurn
			&& action.type.move.pathFraction == 0 )
		{
			// THEN check for interuption.
			// Clear out the current team events, look for "new team"
			// Player: only pauses on "new team"
			// AI: pauses on "new team" OR "new target"
			// No one pauses for Civs.
			int i=0;
			bool newTeam = false;
			while( i < targetEvents.Size() ) {
				TargetEvent t = targetEvents[i];
				if (	( aiArr[currentTeamTurn] || t.team == 1 )	// AI always pause. Players pause on new team.
					 && t.viewerID == currentTeamTurn
					 && units[t.targetID].Team() != CIV_TEAM )
				{
					// New sighting!
					GLOUTPUT(( "Sighting: Team %d sighted target %d on team %d.\n", t.viewerID, t.targetID, units[t.targetID].Team() ));
					newTeam = true;
					targetEvents.SwapRemove( i );
				}
				else {
					++i;
				}
			}
			if ( newTeam ) {
				actionStack.Clear();
			}
		}
	}
}


bool BattleScene::ProcessActionCameraBounds( U32 deltaTime, Action* action )
{
	bool pop = false;
	Vector3F lookingAt;
	float t = Travel( deltaTime, action->type.cameraBounds.speed );
					
	// Don't let it over-shoot!
	engine->CameraLookingAt( &lookingAt );
	Vector3F atToTarget = action->type.cameraBounds.target - lookingAt;

	if ( atToTarget.Length() <= t ) {
		pop = true;
	}
	else {
		Vector2F ui;
		Vector3F d = action->type.cameraBounds.normal * t;
		engine->camera.DeltaPosWC( d.x, d.y, d.z );

		const Screenport& port = engine->GetScreenport();
		port.WorldToUI( action->type.cameraBounds.target, &ui );

		Rectangle2F inset = CalcInsetUIBounds();
		if ( action->type.cameraBounds.center ) {
			Vector2F center = inset.Center();
			inset.min = center;
			inset.max = center;
			inset.Outset( 20 );
		}

		GLOUTPUT(( "Camera (%.1f,%.1f) ui (%.1f,%.1f)-(%.1f,%.1f)\n",
					ui.x, ui.y, 
					inset.min.x, inset.min.y, inset.max.x, inset.max.y ));

		if ( inset.Contains( ui ) ) {
			pop = true;
		}
	}
	return pop;
}


int BattleScene::ProcessAction( U32 deltaTime )
{
	int result = 0;

	if ( !actionStack.Empty() )
	{
		Action* action = actionStack.Top();

		Unit* unit = 0;
		Model* model = 0;

		if ( action->unit ) {
			if ( !action->unit->IsAlive() || !action->unit->GetModel() ) {
				GLASSERT( 0 );	// may be okay, but untested.
				actionStack.Pop();
				return true;
			}
			unit = action->unit;
			model = action->unit->GetModel();
		}

		switch ( action->actionID ) {
			case ACTION_MOVE: 
				{
					// Move the unit. Be careful to never move more than one step (Travel() does not).
					// Used to do intermedia rotation, but it was annoying. Once vision was switched
					// to 360 it did nothing. So rotation is free now.
					//
					float SPEED = 4.5f;
					float x, z, r;

					MoveAction* move = &action->type.move;
						
					move->path.GetPos( action->type.move.pathStep, move->pathFraction, &x, &z, &r );
					// Face in the direction of walking.
					model->SetRotation( r );

					// Move fast when can't be seen:
					if ( move->pathStep < move->path.pathLen-1 ) {
						Vector2<S16> v0 = move->path.GetPathAt( move->pathStep );
						Vector2<S16> v1 = move->path.GetPathAt( move->pathStep+1 );
						if (    !visibility.TeamCanSee( TERRAN_TEAM, v0.x, v0.y )
							 && !visibility.TeamCanSee( TERRAN_TEAM, v1.x, v1.y ) )
						{
							SPEED *= 10.0f;
						}
					}

					float travel = Travel( deltaTime, SPEED );

					while(    (move->pathStep < move->path.pathLen-1 )
						   && travel > 0.0f
						   && (!(result & STEP_COMPLETE))) 
					{
						move->path.Travel( &travel, &move->pathStep, &move->pathFraction );
						if ( move->pathFraction == 0.0f ) {
							// crossed a path boundary.
							GLRELASSERT( unit->TU() >= 0.99 );	// one move is one TU. Should never be less than one, but
																// occasionally see magic floating point number bugs.
							
							Vector2<S16> v0 = move->path.GetPathAt( move->pathStep-1 );
							Vector2<S16> v1 = move->path.GetPathAt( move->pathStep );
							int d = abs( v0.x-v1.x ) + abs( v0.y-v1.y );

							if ( d == 1 )
								unit->UseTU( 1.0f );
							else if ( d == 2 )
								unit->UseTU( 1.41f );
							else { GLRELASSERT( 0 ); }

							visibility.InvalidateUnit( unit-units );
							result |= STEP_COMPLETE;
						}
						move->path.GetPos( move->pathStep, move->pathFraction, &x, &z, &r );
						Vector3F v = { x+0.5f, 0.0f, z+0.5f };
						unit->SetPos( v, model->GetRotation() );
					}
					if ( move->pathStep == move->path.pathLen-1 ) {
						actionStack.Pop();
						visibility.InvalidateUnit( unit-units );
						result |= STEP_COMPLETE | UNIT_ACTION_COMPLETE;
					}
				}
				break;

			case ACTION_ROTATE:
				{
					const float ROTSPEED = 400.0f;
					float travel = Travel( deltaTime, ROTSPEED );

					float delta, bias;
					MinDeltaDegrees( model->GetRotation(), action->type.rotate.rotation, &delta, &bias );

					if ( delta <= travel ) {
						unit->SetYRotation( action->type.rotate.rotation );
						actionStack.Pop();
						result |= UNIT_ACTION_COMPLETE;
					}
					else {
						unit->SetYRotation( model->GetRotation() + bias*travel );
					}
				}
				break;

			case ACTION_SHOOT:
				{
					int r = ProcessActionShoot( action, unit, model );
					result |= r;
				}
				break;

			case ACTION_HIT:
				{
					int r = ProcessActionHit( action );
					result |= r;
				}
				break;

			case ACTION_DELAY:
				{
					if ( deltaTime >= action->type.delay.delay ) {
						actionStack.Pop();
						result |= OTHER_ACTION_COMPLETE;
					}
					else {
						action->type.delay.delay -= deltaTime;
					}
				}
				break;

			case ACTION_CAMERA:
				{
					action->type.camera.timeLeft -= deltaTime;
					if ( action->type.camera.timeLeft > 0 ) {
						Vector3F v;
						for( int i=0; i<3; ++i ) {
							v.X(i) = Interpolate(	(float)action->type.camera.time, 
													action->type.camera.start.X(i),
													0.0f,							
													action->type.camera.end.X(i),
													(float)action->type.camera.timeLeft );
						}
						engine->camera.SetPosWC( v );
					}
					else {
						actionStack.Pop();
						result |= OTHER_ACTION_COMPLETE;
					}
				}
				break;

			case ACTION_CAMERA_BOUNDS:
				{
					if ( ProcessActionCameraBounds( deltaTime, action ) )
						actionStack.Pop();
				}
				break;

/*			case ACTION_LANDER:
				{
					action->type.lander.timeRemaining -= (int)deltaTime;
					if ( action->type.lander.timeRemaining < 0 )
						action->type.lander.timeRemaining = 0;

					if ( action->type.lander.timeRemaining == 0 ) {
						actionStack.Pop();
						engine->GetMap()->SetLanderFlight( 0 );

						for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
							if ( units[i].GetModel() ) {
								Model* m = units[i].GetModel();
								m->ClearFlag( Model::MODEL_INVISIBLE );
							}
						}
					}
					else {
						float flight = (float)(action->type.lander.timeRemaining) / (float)(LanderAction::TOTAL_TIME);
						engine->GetMap()->SetLanderFlight( flight );
					}
				}
				break; */

			default:
				GLRELASSERT( 0 );
				break;
		}
	}
	return result;
}


int BattleScene::ProcessActionShoot( Action* action, Unit* unit, Model* model )
{
	DamageDesc damageDesc;
	bool impact = false;
	Model* modelHit = 0;
	Vector3F intersection;
	U32 delayTime = 0;
	const WeaponItemDef* weaponDef = 0;
	Ray ray;
	WeaponMode mode = action->type.shoot.mode;

	int result = 0;

	if ( unit && model && unit->IsAlive() ) {
		// Shooting announces the units location.
		for( int i=0; i<3; ++i ) {
			if ( aiArr[i] )
				aiArr[i]->Inform( unit, 2 );
		}

		Vector3F p0, p1;
		Vector3F beam0 = { 0, 0, 0 }, beam1 = { 0, 0, 0 };

		const Item* weaponItem = unit->GetWeapon();
		GLRELASSERT( weaponItem );
		weaponDef = weaponItem->GetItemDef()->IsWeapon();
		GLRELASSERT( weaponDef );

		model->CalcTrigger( &p0 );
		p1 = action->type.shoot.target;

		ray.origin = p0;
		ray.direction = p1-p0;

		// What can we hit?
		// model
		//		unit, alive (does damage)
		//		unit, dead (does nothing)
		//		model, world (does damage)
		//		gun, does nothing
		// ground / bounds

		// Don't hit the shooter:
		const Model* ignore[] = { unit->GetModel(), unit->GetWeaponModel(), 0 };
		Model* m = engine->IntersectModel( ray, TEST_TRI, 0, 0, ignore, &intersection );

		if ( m && intersection.y < 0.0f ) {
			// hit ground before the unit (intesection is with the part under ground)
			// The world bounds will pick this up later.
			m = 0;
		}

		weaponDef->DamageBase( mode, &damageDesc );

		if ( m ) {
			impact = true;
			GLASSERT( intersection.x >= 0 && intersection.x <= (float)MAP_SIZE );
			GLASSERT( intersection.z >= 0 && intersection.z <= (float)MAP_SIZE );
			GLASSERT( intersection.y >= 0 && intersection.y <= 10.0f );
			beam0 = p0;
			beam1 = intersection;
			GLASSERT( m->AABB().Contains( intersection ) );
			modelHit = m;
		}
		else {		
			Vector3F in, out;
			int inResult, outResult;
			Rectangle3F worldBounds;
			worldBounds.Set( 0, 0, 0, 
							(float)engine->GetMap()->Width(), 
							8.0f,
							(float)engine->GetMap()->Height() );

			int result = IntersectRayAllAABB( ray.origin, ray.direction, worldBounds, 
											  &inResult, &in, &outResult, &out );

			GLASSERT( result == grinliz::INTERSECT );
			if ( result == grinliz::INTERSECT ) {
				beam0 = p0;
				beam1 = out;
				intersection = out;
				GLASSERT( intersection.x >= 0 && intersection.x <= (float)MAP_SIZE );
				GLASSERT( intersection.z >= 0 && intersection.z <= (float)MAP_SIZE );
				GLASSERT( intersection.y >= 0 && intersection.y <= 10.0f );

				if ( out.y < 0.01 ) {
					// hit the ground
					impact = true;

				}
			}
		}

		if ( beam0 != beam1 ) {
			weaponDef->RenderWeapon( mode,
									 ParticleSystem::Instance(),
									 beam0, beam1, 
									 impact, 
									 game->CurrentTime(), 
									 &delayTime );
			SoundManager::Instance()->QueueSound( weaponDef->weapon[weaponDef->Index(mode)].sound );
		}
	}

	// Messy. Did we hit a target? Problems with code below:
	// - accepts any target as a hit...not necessary what we were aiming for.
	// - Ignore explosive data! It is too hard to get right.

	if ( weaponDef && !weaponDef->IsExplosive( mode ) ) {
		Unit* hitUnit = 0;
		Unit* hitWeapon = 0;
		if ( modelHit ) {
			hitUnit = UnitFromModel( modelHit, false );
			if ( hitUnit && hitUnit->Team() == unit->Team() )
				hitUnit = 0;	// don't count friendly fire
			hitWeapon = UnitFromModel( modelHit, true );
			if ( hitWeapon && hitWeapon->Team() == unit->Team() )
				hitWeapon = 0;
		}
		WeaponItemDef::AddAccData( action->type.shoot.chanceToHit, hitUnit || hitWeapon );
	}

	actionStack.Pop();
	result |= UNIT_ACTION_COMPLETE;
	action = 0;	// invalidated by pop!!

	if ( impact ) {
		GLRELASSERT( weaponDef );
		GLASSERT( intersection.x >= 0 && intersection.x <= (float)MAP_SIZE );
		GLASSERT( intersection.z >= 0 && intersection.z <= (float)MAP_SIZE );
		GLASSERT( intersection.y >= 0 && intersection.y <= 10.0f );

		Action *h = actionStack.Push();
		h->Init( ACTION_HIT, unit );
		h->type.hit.damageDesc = damageDesc;
		h->type.hit.explosive = weaponDef->IsExplosive( mode );
		h->type.hit.p = intersection;
		
		h->type.hit.n = ray.direction;
		h->type.hit.n.Normalize();

		h->type.hit.m = modelHit;
	}

	if ( delayTime ) {
		Action* a = actionStack.Push();
		a->Init( ACTION_DELAY, 0 );
		a->type.delay.delay = delayTime;
	}

	if ( impact && modelHit ) {
		int x = LRintf( intersection.x );
		int y = LRintf( intersection.z );
		if ( engine->GetMap()->GetFogOfWar().IsSet( x, y, 0 ) ) {
			PushScrollOnScreen( intersection );
		}
	}

	return result;
}


int BattleScene::ProcessActionHit( Action* action )
{
	Rectangle2I destroyed;
	destroyed.SetInvalid();
	int result = 0;
	static const int MAX_EXPLOSION = 8;
	Vector2I explosion[MAX_EXPLOSION];
	int nExplosion = 0;

	if ( !action->type.hit.explosive ) {
		SoundManager::Instance()->QueueSound( "hit" );

		// Apply direct hit damage
		Model* m = action->type.hit.m;
		Unit* hitUnit = 0;
		Unit* hitWeapon = 0;

		if ( m ) {
			hitUnit = UnitFromModel( m, false );
			hitWeapon = UnitFromModel( m, true );
		}

		if ( hitUnit ) {
			if ( hitUnit->IsAlive() ) {
				hitUnit->DoDamage( action->type.hit.damageDesc, engine->GetMap() );
				if ( !hitUnit->IsAlive() ) {
					selection.ClearTarget();			
					visibility.InvalidateUnit( hitUnit - units );
					if ( action->unit )
						action->unit->CreditKill();
				}
				GLOUTPUT(( "Hit Unit 0x%x hp=%d/%d\n", (unsigned)hitUnit, (int)hitUnit->HP(), (int)hitUnit->GetStats().TotalHP() ));
			}
		}
		else if ( hitWeapon ) {
			Inventory* inv = hitWeapon->GetInventory();
			inv->RemoveItem( Inventory::WEAPON_SLOT );
			hitWeapon->UpdateInventory();
			GLOUTPUT(( "Shot the weapon out of Unit 0x%x hand.\n", (unsigned)hitWeapon ));
		}
		else if ( m && m->IsFlagSet( Model::MODEL_OWNED_BY_MAP ) ) {
			Rectangle2I bounds;
			engine->GetMap()->MapBoundsOfModel( m, &bounds );

			// Hit world object.
			Vector2I exp = { -1, -1 };
			engine->GetMap()->DoDamage( m, action->type.hit.damageDesc, &destroyed, &exp );
			if ( exp.x >= 0 )
				explosion[nExplosion++] = exp;
		}
	}
	else {
		// There is a small offset to move the explosion back towards the shooter.
		// If it hits a wall (common) this will move it to the previous square.
		// Also means a model hit may be a "near miss"...but explosions are messy.
		explosion[0].Set(	(int)(action->type.hit.p.x - 0.2f*action->type.hit.n.x), 
							(int)(action->type.hit.p.z - 0.2f*action->type.hit.n.z) );
		nExplosion = 1;
	}


	if ( nExplosion ) {
		// Explosion
		SoundManager::Instance()->QueueSound( "explosion" );

		const int MAX_RAD = 2;
		const int MAX_RAD_2 = MAX_RAD*MAX_RAD;
		Rectangle2I mapBounds = engine->GetMap()->Bounds();

		while ( nExplosion ) {
			// generates smoke:
			int x0 = explosion[nExplosion-1].x;
			int y0 = explosion[nExplosion-1].y;

			destroyed.Set( x0-MAX_RAD, y0-MAX_RAD, x0+MAX_RAD, y0+MAX_RAD );

			for( int rad=0; rad<=MAX_RAD; ++rad ) {
				DamageDesc dd = action->type.hit.damageDesc;
				dd.Scale( (float)(1+MAX_RAD-rad) / (float)(1+MAX_RAD) );

				for( int y=y0-rad; y<=y0+rad; ++y ) {
					for( int x=x0-rad; x<=x0+rad; ++x ) {
						if ( x==(x0-rad) || x==(x0+rad) || y==(y0-rad) || y==(y0+rad) ) {

							Vector2I x0y0 = { x, y };
							if ( !mapBounds.Contains( x0y0 ) )	// keep explosions in-bounds
								continue;

							// Tried to do this with the pather, but the issue with 
							// walls being on the inside and outside of squares got 
							// ugly. But the other option - ray casting - also nasty.
							// Go one further than could be walked.
							int radius2 = (x-x0)*(x-x0) + (y-y0)*(y-y0);
							if ( radius2 > MAX_RAD_2 )
								continue;
						
							// can the tile to be damaged be reached by the explosion?
							// visibility is checked up to the tile before this one, else
							// it is possible to miss because "you can't see yourself"
							bool canSee = true;
							if ( rad > 0 ) {
								LineWalk walk( x0, y0, x, y );
								// Actually 'less than' so that damage goes through walls a little.
								while( walk.CurrentStep() < walk.NumSteps() ) {
									Vector2I p = walk.P();
									Vector2I q = walk.Q();

									// Was using CanSee, but that has the problem that
									// some walls are inner and some walls are outer.
									// *sigh* Use the pather.
									if ( !engine->GetMap()->CanSee( p, q ) ) {
										canSee = false;
										break;
									}
									walk.Step();
								}
								// Go with the sight check to see if the explosion can
								// reach this tile.
								if ( !canSee )
									continue;
							}

							Unit* unit = GetUnitFromTile( x, y );
							if ( unit && unit->IsAlive() ) {
								unit->DoDamage( dd, engine->GetMap() );
								if ( !unit->IsAlive() ) {
									visibility.InvalidateUnit( unit - units );
									if ( unit == SelectedSoldierUnit() ) {
										selection.ClearTarget();			
									}
									if ( action->unit )
										action->unit->CreditKill();
								}
							}
							bool hitAnything = false;
							Vector2I exp = { -1, -1 };
							engine->GetMap()->DoDamage( x, y, dd, &destroyed, &exp );
							if ( exp.x >= 0 && nExplosion < MAX_EXPLOSION )
								explosion[nExplosion++] = exp;

							// Where to add smoke?
							// - if we hit anything
							// - change of smoke anyway
							if ( hitAnything || random.Bit() ) {
								int turns = 4 + random.Rand( 4 );
								engine->GetMap()->AddSmoke( x, y, turns );
							}
						}
					}
				}
			}
			nExplosion--;
		}
	}
	visibility.InvalidateAll( destroyed ); 
	actionStack.Pop();
	result |= UNIT_ACTION_COMPLETE;
	return true;
}


void BattleScene::HandleHotKeyMask( int mask )
{
	if ( mask & GAME_HK_NEXT_UNIT ) {
		HandleNextUnit( 1 );		
	}
	if ( mask & GAME_HK_PREV_UNIT ) {
		HandleNextUnit( -1 );		
	}
	if ( mask & GAME_HK_ROTATE_CCW ) {
		HandleRotation( 45.f );
	}
	if ( mask & GAME_HK_ROTATE_CW ) {
		HandleRotation( -45.f );
	}
	if ( mask & GAME_HK_TOGGLE_ROTATION_UI ) {
		bool visible = !controlButton[ROTATE_CCW_BUTTON].Visible();
		controlButton[ROTATE_CCW_BUTTON].SetVisible( visible );
		controlButton[ROTATE_CW_BUTTON].SetVisible( visible );
	}
	if ( mask & GAME_HK_TOGGLE_NEXT_UI ) {
		bool visible = !controlButton[NEXT_BUTTON].Visible();
		controlButton[NEXT_BUTTON].SetVisible( visible );
		controlButton[PREV_BUTTON].SetVisible( visible );

	}
}


void BattleScene::HandleNextUnit( int bias )
{
	int subTurnIndex = subTurnCount;
	if ( SelectedSoldierUnit() ) {
		for( int i=0; i<subTurnCount; ++i ) {
			if ( subTurnOrder[i] == SelectedSoldierUnit() - units ) {
				subTurnIndex = i;
				break;
			}
		}
	}

	while ( true ) {
		subTurnIndex += bias;
		if ( subTurnIndex < 0 )
			subTurnIndex = subTurnCount;
		if ( subTurnIndex > subTurnCount )
			subTurnIndex = 0;

		// This condition will always terminated the loop.
		if ( subTurnIndex == subTurnCount ) {
			SetSelection( 0 );
			break;
		}
		else {
			int index = subTurnOrder[ subTurnIndex ];
			GLRELASSERT( index >= TERRAN_UNITS_START && index < TERRAN_UNITS_END );

			if ( units[index].IsAlive() ) {
				SetSelection( &units[index] );
				PushScrollOnScreen( units[index].GetModel()->Pos() );
				break;
			}
		}
	}
	//SoundManager::Instance()->QueueSound( "blip" );
}


void BattleScene::HandleRotation( float bias )
{
	if ( actionStack.Empty() && SelectedSoldierUnit() ) {
		Unit* unit = SelectedSoldierUnit();

		float r = unit->GetModel()->GetRotation();
		r += bias;
		r = NormalizeAngleDegrees( r );
		r = 45.f * float( (int)((r+20.0f) / 45.f) );

		Action* action = actionStack.Push();
		action->Init( ACTION_ROTATE, unit );
		action->type.rotate.rotation = r;
	}
}


bool BattleScene::HandleIconTap( const gamui::UIItem* tapped )
{
	if ( selection.FireMode() ) {
		bool okay = false;
		WeaponMode mode = kSnapFireMode;

		if ( tapped == fireButton+0 )		{ okay = true; mode = kSnapFireMode; }
		else if ( tapped == fireButton+1 )	{ okay = true; mode = kAutoFireMode; }
		else if ( tapped == fireButton+2 )	{ okay = true; mode = kAltFireMode;  }
			
		if ( okay ) { 
			// shooting creates a turn action then a shoot action.
			GLRELASSERT( selection.soldierUnit >= 0 );
			GLRELASSERT( selection.targetUnit >= 0 );
			//SoundManager::Instance()->QueueSound( "blip" );

			Item* weapon = selection.soldierUnit->GetWeapon();
			GLRELASSERT( weapon );
			const WeaponItemDef* wid = weapon->GetItemDef()->IsWeapon();
			GLRELASSERT( wid );

			Vector3F target;
			float targetWidth = 0.0f;
			float targetHeight = 0.0f;
			if ( selection.targetPos.x >= 0 ) {
				target.Set( (float)selection.targetPos.x + 0.5f, 1.0f, (float)selection.targetPos.y + 0.5f );
			}
			else {
				selection.targetUnit->GetModel()->CalcTarget( &target );
				selection.targetUnit->GetModel()->CalcTargetSize( &targetWidth, &targetHeight );
			}
			PushShootAction( selection.soldierUnit, target, targetWidth, targetHeight, mode, 1.0f, false );
		}
		selection.targetUnit = 0;
		selection.targetPos.Set( -1, -1 );
		targetButton.SetUp();
	}
	else {
		if ( tapped == controlButton + ROTATE_CCW_BUTTON ) {
			HandleRotation( 45.f );
		}
		else if ( tapped == controlButton + ROTATE_CW_BUTTON ) {
			HandleRotation( -45.f );
		}
		else if ( tapped == &invButton ) {
			if ( actionStack.Empty() && SelectedSoldierUnit() ) {
				CharacterSceneData* input = new CharacterSceneData();
				input->unit = SelectedSoldierUnit();
				game->PushScene( Game::CHARACTER_SCENE, input );
			}
		}
		else if ( tapped == &nextTurnButton ) {
			SetSelection( 0 );
			engine->GetMap()->ClearNearPath();
			if ( EndCondition( &tacticalData ) ) {
				game->PushScene( Game::END_SCENE, new TacticalEndSceneData( tacticalData ) );
			}
			else {
				NextTurn( true );
			}
		}
		else if ( tapped == controlButton + NEXT_BUTTON ) {
			HandleNextUnit( 1 );
		}
		else if ( tapped == controlButton + PREV_BUTTON ) {
			HandleNextUnit( -1 );
		}
		else if ( tapped == &helpButton ) {
			game->PushScene( Game::HELP_SCENE, new HelpSceneData( "tacticalHelp") );
		}
		else if ( tapped == &exitButton ) {
			DialogSceneData* data = new DialogSceneData();
			data->type = DialogSceneData::DS_YESNO;
			data->text = "Do you wish to evacuate? All units not in lander will be lost.";

			game->PushScene( Game::DIALOG_SCENE, data );
		}
	}
	return tapped != 0;
}


void BattleScene::SceneResult( int sceneID, int result )
{
	const Model* model = engine->GetMap()->GetLanderModel();
	Rectangle2I bounds;
	engine->GetMap()->MapBoundsOfModel( model, &bounds );

	if ( sceneID == Game::DIALOG_SCENE && result ) {
		// Exit!
		for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
			if ( units[i].InUse() ) {
				Vector2I v = units[i].Pos();
				if ( !bounds.Contains( v ) ) {
					units[i].Leave();
				}
			}
		}
		EndCondition( &tacticalData );	// fills out tactical data...
		game->PushScene( Game::END_SCENE, new TacticalEndSceneData( tacticalData ) );
	}
}


/*
	Handle a tap.

	item			down				move		cancel		up
	----			----				----		----		----
	Button			capture (gamui)		x			x			handle		Sent to gamui. Need only to note if gamui captured the tap.
	Unit/Tile		select				moveTo		no move		move		Select on down is a bummer...not sure how else to handle
	World/Tile		x					drag		x			x
*/
void BattleScene::Tap(	int action, 
						const grinliz::Vector2F& view,
						const grinliz::Ray& world )
{
#if 0
	{
		// Test projections.
		Vector3F pos;
		IntersectRayPlane( world.origin, world.direction, 1, 0.0f, &pos );
		pos.y = 0.01f;
		Color4F c = { 1, 0, 0, 1 };
		Color4F cv = { 0, 0, 0, 0 };
		ParticleSystem::Instance()->EmitOnePoint( c, cv, pos, 1500 );
	}
#endif
#if 0
	{
		// Test Sound.
		int size=0;
		const gamedb::Reader* reader = game->GetDatabase();
		const gamedb::Item* data = reader->Root()->Child( "data" );
		const gamedb::Item* item = data->Child( "testlaser44" );

		const void* snd = reader->AccessData( item, "binary", &size );
		GLASSERT( snd );
		PlayWAVSound( snd, size );
	}
#endif

	bool uiActive = actionStack.Empty() && (currentTeamTurn == TERRAN_TEAM);
	Vector2F ui;
	engine->GetScreenport().ViewToUI( view, &ui );
	bool processTap = false;


	if ( action == GAME_TAP_DOWN || action == GAME_TAP_DOWN_PANNING ) {
		if ( uiActive && action == GAME_TAP_DOWN ) {
			// First check buttons.
			gamui2D.TapDown( ui.x, ui.y );
			if ( !gamui2D.TapCaptured() ) {
				gamui3D.TapDown( ui.x, ui.y );
			}
		}

		if ( !GamuiHasCapture() ) {
			Drag( action, uiActive, view );
		}
	}
	else if ( action == GAME_TAP_MOVE || action == GAME_TAP_MOVE_PANNING ) {
		if ( isDragging ) {
			Drag( action, uiActive, view );
		}
	}
	else if ( action == GAME_TAP_UP || action == GAME_TAP_UP_PANNING ) {
		if ( isDragging ) {
			Drag( action, uiActive, view );

			if (    action == GAME_TAP_UP
				 && dragLength <= 1.0f 
				 && actionStack.Empty() )
			{
				processTap = true;
			}
		}
		
		if ( GamuiHasCapture() ) {
			const gamui::UIItem* item0 = gamui2D.TapUp( ui.x, ui.y );
			const gamui::UIItem* item1 = gamui3D.TapUp( ui.x, ui.y );
			const gamui::UIItem* item = item0 ? item0 : item1;

			if ( item ) {
				HandleIconTap( item );
			}
		}
		if (    action == GAME_TAP_UP 
			 && dragLength < 1.0f 
			 && selection.FireMode() ) 
		{
			// Whether or not something was selected, drop back to normal mode.
			selection.targetPos.Set( -1, -1 );
			selection.targetUnit = 0;
			targetButton.SetUp();
			processTap = false;
		}
	}
	else if ( action == GAME_TAP_CANCEL || action == GAME_TAP_CANCEL_PANNING ) {
		processTap = false;
		if ( isDragging ) {
			Drag( action, uiActive, view );
		}
		else {
			gamui2D.TapUp( ui.x, ui.y );
			gamui3D.TapUp( ui.x, ui.y );
		}
	}

	if ( processTap ) {

		if ( Engine::mapMakerMode ) {
			const Vector3F& pos = mapmaker_mapSelection->Pos();
			int rotation = (int) (mapmaker_mapSelection->GetRotation() / 90.0f );

			int ix = (int)pos.x;
			int iz = (int)pos.z;
			if (    ix >= 0 && ix < engine->GetMap()->Width()
	  			 && iz >= 0 && iz < engine->GetMap()->Height() ) 
			{
				if ( *engine->GetMap()->GetItemDefName( mapmaker_currentMapItem ) ) {
					engine->GetMap()->AddItem( ix, iz, rotation, mapmaker_currentMapItem, -1, 0 );
				}
				else if ( mapmaker_currentMapItem == 0x70 ) { // smoke
					engine->GetMap()->SetPyro( ix, iz, 21, false );
				}
				else if ( mapmaker_currentMapItem == 0x71 ) { // fire
					engine->GetMap()->SetPyro( ix, iz, 17, true );
				}
			}
		}

		// Get the map intersection. May be used by TARGET_TILE or NORMAL
		Vector3F intersect;
		Map* map = engine->GetMap();

		Vector2I tilePos = { 0, 0 };
		bool hasTilePos = false;

		int result = IntersectRayPlane( world.origin, world.direction, 1, 0.0f, &intersect );
		if ( result == grinliz::INTERSECT && intersect.x >= 0 && intersect.x < Map::SIZE && intersect.z >= 0 && intersect.z < Map::SIZE ) {
			tilePos.Set( (int)intersect.x, (int) intersect.z );
			hasTilePos = true;
		}

		if ( targetButton.Down() ) {
			// Targeting a tile:
			if ( hasTilePos ) {
				selection.targetUnit = 0;
				selection.targetPos.Set( tilePos.x, tilePos.y );
			}
			return;
		}

		Model* tappedModel = 0;
		const Unit* tappedUnit = 0;

		// Priorities:
		//   1. Alien or Terran on that tile.
		//   2. Alien tapped

		// If there is a selected model, then we can tap a target model.
		bool canSelectAlien = SelectedSoldier();			// a soldier is selected

		for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
			if ( units[i].GetModel() ) {
				if ( units[i].IsAlive() ) {
					units[i].GetModel()->SetFlag( Model::MODEL_SELECTABLE );
					if ( units[i].Pos() == tilePos ) {
						GLRELASSERT( !tappedUnit );
						tappedUnit = units + i;
						tappedModel = units[i].GetModel();
					}
				}
				else {
					units[i].GetModel()->ClearFlag( Model::MODEL_SELECTABLE );
				}
			}
		}
		for( int i=ALIEN_UNITS_START; i<ALIEN_UNITS_END; ++i ) {
			if ( units[i].GetModel() ) {
				if ( canSelectAlien && units[i].IsAlive() ) {
					units[i].GetModel()->SetFlag( Model::MODEL_SELECTABLE );
					if ( units[i].Pos() == tilePos ) {
						GLRELASSERT( !tappedUnit );
						tappedUnit = units + i;
						tappedModel = units[i].GetModel();
					}
				}
				else {
					units[i].GetModel()->ClearFlag( Model::MODEL_SELECTABLE );
				}
			}
		}

		if ( !tappedUnit ) {
			// can possible still select alien on intersection.
			Model* m = engine->IntersectModel( world, TEST_HIT_AABB, Model::MODEL_SELECTABLE, 0, 0, 0 );
			if ( m ) {
				tappedModel = m;
				tappedUnit = UnitFromModel( tappedModel );
				if ( tappedUnit->Team() != ALIEN_TEAM ) {
					// roll back! this prevents selecting rather than moving when units are all grouped up.
					tappedModel = 0;
					tappedUnit = 0;
				}
			}
		}

		if ( tappedModel && tappedUnit ) {
			SetSelection( const_cast< Unit* >( tappedUnit ));		// sets either the Alien or the Unit
		}
		if ( SelectedSoldierUnit() && !tappedUnit && hasTilePos ) {
			// Not a model - use the tile
			Vector2<S16> start   = { (S16)SelectedSoldierModel()->X(), (S16)SelectedSoldierModel()->Z() };
			Vector2<S16> end = { (S16)tilePos.x, (S16)tilePos.y };

			// Compute the path:
			float cost;
			const Stats& stats = selection.soldierUnit->GetStats();

			int result = engine->GetMap()->SolvePath( selection.soldierUnit, start, end, &cost, &pathCache );
			if ( result == micropather::MicroPather::SOLVED && cost <= selection.soldierUnit->TU() ) {
				// Go!
				Action* action = actionStack.Push();
				action->Init( ACTION_MOVE, SelectedSoldierUnit() );
				action->type.move.path.Init( pathCache );
				engine->GetMap()->ClearNearPath();
			}
		}
	}
}


void BattleScene::ShowNearPath( const Unit* unit )
{
	if ( unit == 0 && nearPathState.unit == 0 )		
		return;		// drawing nothing correctly
	if (    unit == nearPathState.unit
		 && unit->TU() == nearPathState.tu
		 && unit->Pos() == nearPathState.pos ) {
			return;		// drawing something that is current.
	}

	nearPathState.Clear();
	engine->GetMap()->ClearNearPath();

	if ( unit && unit->GetModel() ) {

		float autoTU = 0.0f;
		float snappedTU = 0.0f;

		if ( unit->GetWeapon() ) {
			const WeaponItemDef* wid = unit->GetWeapon()->GetItemDef()->IsWeapon();
			snappedTU = unit->FireTimeUnits( kSnapFireMode );
			autoTU = unit->FireTimeUnits( kAutoFireMode );
		}

		nearPathState.unit = unit;
		nearPathState.tu = unit->TU();
		nearPathState.pos = unit->Pos();

		Vector2<S16> start = { (S16)unit->Pos().x, (S16)unit->Pos().y };
		float tu = unit->TU();

		Vector2F range[3] = { 
			{ 0.0f,	tu-autoTU },
			{ tu-autoTU, tu-snappedTU },
			{ tu-snappedTU, tu }
		};
		engine->GetMap()->ShowNearPath( unit->Pos(), unit, start, tu, range );
	}
}


void BattleScene::MakePathBlockCurrent( Map* map, const void* user )
{
	const Unit* exclude = (const Unit*)user;
	GLRELASSERT( exclude >= units && exclude < &units[MAX_UNITS] );

	grinliz::BitArray<MAP_SIZE, MAP_SIZE, 1> block;

	for( int i=0; i<MAX_UNITS; ++i ) {
		if (    units[i].IsAlive() 
			 && units[i].GetModel() 
			 && &units[i] != exclude ) // oops - don't cause self to not path
		{
			int x = (int)units[i].GetModel()->X();
			int z = (int)units[i].GetModel()->Z();
			block.Set( x, z );
		}
	}
	// Checks for equality before the reset.
	map->SetPathBlocks( block );
}

Unit* BattleScene::UnitFromModel( Model* m, bool useWeaponModel )
{
	if ( m ) {
		for( int i=0; i<MAX_UNITS; ++i ) {
			if (	( !useWeaponModel && units[i].GetModel() == m )
				 || ( useWeaponModel && units[i].GetWeaponModel() == m ) )
				return &units[i];
		}
	}
	return 0;
}


Unit* BattleScene::GetUnitFromTile( int x, int z )
{
	for( int i=0; i<MAX_UNITS; ++i ) {
		if ( units[i].GetModel() ) {
			int ux = (int)units[i].GetModel()->X();
			if ( ux == x ) {
				int uz = (int)units[i].GetModel()->Z();
				if ( uz == z ) { 
					return &units[i];
				}
			}
		}
	}
	return 0;
}


void BattleScene::DumpTargetEvents()
{
	for( int i=0; i<targetEvents.Size(); ++i ) {
		const TargetEvent& e = targetEvents[i];
		const char* teams[] = { "Terran", "Civ", "Alien" };

		if ( !e.team ) {
			GLOUTPUT(( "%s Unit %d %s %d\n",
						teams[ units[e.viewerID].Team() ],
						e.viewerID, 
						teams[ units[e.targetID].Team() ],
						e.targetID ));
		}
		else {
			GLOUTPUT(( "%s Team %s %d\n",
						teams[ e.viewerID ],
						teams[ units[e.targetID].Team() ],
						e.targetID ));
		}
	}
}


void BattleScene::CalcTeamTargets()
{
	GRINLIZ_PERFTRACK
	// generate events.
	// - if team gets/loses target
	// - if unit gets/loses target

	grinliz::BitArray<MAX_UNITS, MAX_UNITS, 1> newUnitVis;
	visibility.CalcVisMap( &newUnitVis );

	const static Vector2I range[3] = {
		TERRAN_UNITS_START, TERRAN_UNITS_END,
		CIV_UNITS_START, CIV_UNITS_END,
		ALIEN_UNITS_START, ALIEN_UNITS_END
	};

	for( int src=0; src<MAX_UNITS; ++src ) {
		if ( !units[src].IsAlive() )
			continue;

		for( int dst=0; dst<MAX_UNITS; ++dst ) {
			if (    !units[src].IsAlive()
				 || units[src].Team() == units[dst].Team() )	// Don't generate messages about team mates.
			{
				 continue;
			}

			// check unit change - did we see something new?
			if ( !unitVis.IsSet( src, dst ) && newUnitVis.IsSet( src, dst ) )
			{
				int srcTeam = units[src].Team();

				TargetEvent e = { 0, src, dst };
				targetEvents.Push( e );

				// Check team change.
				Rectangle2I teamRange;
				teamRange.Set( range[srcTeam].x, dst, range[srcTeam].y, dst );
				// No one on this team, prior to this check, could see the unit.
				if ( !unitVis.IsRectSet( teamRange ) )
				{	
					TargetEvent e = { 1, srcTeam, dst };
					targetEvents.Push( e );
				}
			}
		}
	}
	unitVis = newUnitVis;
}


void BattleScene::Drag( int action, bool uiActivated, const grinliz::Vector2F& view )
{
	Vector2F ui;
	engine->GetScreenport().ViewToUI( view, &ui );
	
	bool panning = (action & GAME_TAP_PANNING) ? true : false;
	action = action & GAME_TAP_MASK;
	
	switch ( action ) 
	{
		case GAME_TAP_DOWN:
		{
			GLASSERT( isDragging == false );
			isDragging = true;
			dragLength = 0;

			Ray ray;
			engine->GetScreenport().ViewProjectionInverse3D( &dragMVPI );
			engine->RayFromViewToYPlane( view, dragMVPI, &ray, &dragStart );
			dragStartCameraWC = engine->camera.PosWC();
			dragEnd = dragStart;

			// Drag a unit or drag the camera?
			if ( uiActivated && !panning ) {
				Vector2I mapPos = { (int)dragStart.x, (int)dragStart.z };
				for( int i=TERRAN_UNITS_START; i<TERRAN_UNITS_END; ++i ) {
					if ( units[i].IsAlive() && ( mapPos == units[i].Pos() ) ) {
						dragUnit = units + i;
						if ( selection.soldierUnit != dragUnit )
							SetSelection( dragUnit );

						dragBar[0].SetSize( (float)engine->GetMap()->Width(), 0.5f );
						dragBar[1].SetSize( 0.5f, (float)engine->GetMap()->Height() );

						break;
					}
				}
			}
		}
		break;

		case GAME_TAP_MOVE:
		{
			GLASSERT( isDragging );
			if ( dragUnit ) {
				Matrix4 mvpi;
				Ray ray;
				Vector3F intersection;

				engine->GetScreenport().ViewProjectionInverse3D( &mvpi );
				engine->RayFromViewToYPlane( view, mvpi, &ray, &intersection );

				Vector2<S16> start = { (S16)selection.soldierUnit->Pos().x, (S16)selection.soldierUnit->Pos().y };
				Vector2<S16> end   = { (S16)intersection.x, (S16)intersection.z };

				bool visible = false;
				if (    end != start 
					 && end.x >= 0 && end.x < engine->GetMap()->Width() 
					 && end.y >= 0 && end.y < engine->GetMap()->Height() ) 
				{
					float cost;
					gamui::RenderAtom atom;

					int result = engine->GetMap()->SolvePath( selection.soldierUnit, start, end, &cost, &pathCache );
					if ( result == micropather::MicroPather::SOLVED ) {
						int tuLeft = selection.soldierUnit->CalcWeaponTURemaining( cost );
						visible = true;
						if ( tuLeft >= Unit::AUTO_SHOT ) {
							atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_GREEN, UIRenderer::PALETTE_GREEN, 0, 1, 1 );
						}
						else if ( tuLeft == Unit::SNAP_SHOT ) {
							atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_YELLOW, UIRenderer::PALETTE_YELLOW, 0, 1, 1 );
						}
						else if ( cost <= selection.soldierUnit->TU() ) {
							atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_YELLOW, UIRenderer::PALETTE_RED, 0, 1, 1 );
						}
						else {
							atom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_RED, UIRenderer::PALETTE_RED, 0, 1, 1 );
						}
						dragBar[0].SetPos( 0, (float)end.y+0.25f );
						dragBar[1].SetPos( (float)end.x+0.25f, 0 );
					}
					engine->GetMap()->ClearNearPath();
					atom.renderState = (const void*) Map::RENDERSTATE_MAP_NORMAL;
					dragBar[0].SetAtom( atom );
					dragBar[1].SetAtom( atom );
				}
				dragBar[0].SetVisible( visible );
				dragBar[1].SetVisible( visible );
			}
			else {
				Ray ray;
				Vector3F drag;
				engine->RayFromViewToYPlane( view, dragMVPI, &ray, &drag );

				Vector3F delta = drag - dragStart;
				delta.y = 0;
				drag.y = 0;

				dragLength += (drag-dragEnd).Length();
				dragEnd = drag;

				//GLOUTPUT(( "GAME_TAP_MOVE delta=%.2f,%.2f\n", delta.x, delta.z ));
				engine->camera.SetPosWC( dragStartCameraWC - delta );
				engine->RestrictCamera();
			}
		}
		break;

		case GAME_TAP_UP:
		{
			if ( dragUnit ) {
				Matrix4 mvpi;
				Ray ray;
				Vector3F intersection;

				engine->GetScreenport().ViewProjectionInverse3D( &mvpi );
				engine->RayFromViewToYPlane( view, mvpi, &ray, &intersection );

				Vector2<S16> start = { (S16)selection.soldierUnit->Pos().x, (S16)selection.soldierUnit->Pos().y };
				Vector2<S16> end   = { (S16)intersection.x, (S16)intersection.z };

				if (    end != start 
					 && end.x >= 0 && end.x < engine->GetMap()->Width() 
					 && end.y >= 0 && end.y < engine->GetMap()->Height() ) 
				{
					float cost;

					int result = engine->GetMap()->SolvePath( selection.soldierUnit, start, end, &cost, &pathCache );
					if ( result == micropather::MicroPather::SOLVED && cost <= selection.soldierUnit->TU() ) {
						// TU for a move gets used up "as we go" to account for reaction fire and changes.
						// Go!
						Action* action = actionStack.Push();
						action->Init( ACTION_MOVE, SelectedSoldierUnit() );
						action->type.move.path.Init( pathCache );
					}
				}
				dragUnit = 0;
				dragBar[0].SetVisible( false );
				dragBar[1].SetVisible( false );
				nearPathState.Clear();
			}
			else {
				Ray ray;
				Vector3F drag;
				engine->RayFromViewToYPlane( view, dragMVPI, &ray, &drag );

				Vector3F delta = drag - dragStart;
				delta.y = 0;
				drag.y = 0;
				dragLength += (drag-dragEnd).Length();
				dragEnd = drag;

				//GLOUTPUT(( "GAME_TAP_UP delta=%.2f,%.2f\n", delta.x, delta.z ));
				engine->camera.SetPosWC( dragStartCameraWC - delta );
				engine->RestrictCamera();
			}
			isDragging = false;
		}
		break;

		case GAME_TAP_CANCEL:
		{
			if ( dragUnit ) {
				dragUnit = 0;
				dragBar[0].SetVisible( false );
				dragBar[1].SetVisible( false );
				nearPathState.Clear();
			}
			isDragging = false;
		}
		break;

		default:
			GLRELASSERT( 0 );
			break;
	}
}


void BattleScene::Zoom( int style, float delta )
{
	if ( style == GAME_ZOOM_PINCH )
		engine->SetZoom( engine->GetZoom() *( 1.0f+delta) );
	else
		engine->SetZoom( engine->GetZoom() + delta );
}


void BattleScene::Rotate( float degrees )
{
	engine->camera.Orbit( degrees );
}


void BattleScene::DrawHUD()
{
	if ( Engine::mapMakerMode ) {
		nameRankUI.SetVisible( false );
		if ( !game->IsTextSuppressed() ) {
			engine->GetMap()->DumpTile( (int)mapmaker_mapSelection->X(), (int)mapmaker_mapSelection->Z() );

			const char* desc = SelectionDesc();
			UFOText::Draw( 0,  16, "(%2d,%2d) 0x%2x:'%s'", 
						   (int)mapmaker_mapSelection->X(), (int)mapmaker_mapSelection->Z(),
						   mapmaker_currentMapItem, desc );
		}
	}
	else {
		{
			bool enabled = SelectedSoldierUnit() && actionStack.Empty() && targetButton.Up();
			targetButton.SetEnabled( enabled );
			invButton.SetEnabled( enabled );
			controlButton[ROTATE_CCW_BUTTON].SetEnabled( enabled );
			controlButton[ROTATE_CW_BUTTON].SetEnabled( enabled );
		}
		{
			bool enabled = actionStack.Empty() && targetButton.Up();
			controlButton[NEXT_BUTTON].SetEnabled( enabled );
			controlButton[PREV_BUTTON].SetEnabled( enabled );
			exitButton.SetEnabled( enabled );
			nextTurnButton.SetEnabled( enabled );
			helpButton.SetEnabled( enabled );
		}
		// overrides enabled, above.
		if ( targetButton.Down() ) {
			targetButton.SetEnabled( true );
		}
	
		const int CYCLE = 5000;
		float rotation = (float)(game->CurrentTime() % CYCLE)*(360.0f/(float)CYCLE);
		if ( rotation > 90 && rotation < 270 )
			rotation += 180;
		
		if ( alienImage.Visible() ) {
			alienImage.SetRotationY( rotation );
		}

		nameRankUI.SetVisible( SelectedSoldierUnit() != 0 );
		nameRankUI.Set( 50, 0, SelectedSoldierUnit(), true );
	}
}


float MotionPath::DeltaToRotation( int dx, int dy )
{
	float rot = 0.0f;
	GLRELASSERT( dx || dy );
	GLRELASSERT( dx >= -1 && dx <= 1 );
	GLRELASSERT( dy >= -1 && dy <= 1 );

	if ( dx == 1 ) 
		if ( dy == 1 )
			rot = 45.0f;
		else if ( dy == 0 )
			rot = 90.0f;
		else
			rot = 135.0f;
	else if ( dx == 0 )
		if ( dy == 1 )
			rot = 0.0f;
		else
			rot = 180.0f;
	else
		if ( dy == 1 )
			rot = 315.0f;
		else if ( dy == 0 )
			rot = 270.0f;
		else
			rot = 225.0f;
	return rot;
}


void MotionPath::Init( const MP_VECTOR< Vector2<S16> >& pathCache ) 
{
	GLRELASSERT( pathCache.size() <= MAX_TU );
	GLRELASSERT( pathCache.size() > 1 );	// at least start and end

	pathLen = (int)pathCache.size();
	for( unsigned i=0; i<pathCache.size(); ++i ) {
		GLRELASSERT( pathCache[i].x < 256 );
		GLRELASSERT( pathCache[i].y < 256 );
		pathData[i*2+0] = (U8) pathCache[i].x;
		pathData[i*2+1] = (U8) pathCache[i].y;
	}
}

		
void MotionPath::CalcDelta( int i0, int i1, grinliz::Vector2I* vec, float* rot )
{
	Vector2<S16> path0 = GetPathAt( i0 );
	Vector2<S16> path1 = GetPathAt( i1 );

	int dx = path1.x - path0.x;
	int dy = path1.y - path0.y;
	if ( vec ) {
		vec->x = dx;
		vec->y = dy;
	}
	if ( rot ) {
		*rot = DeltaToRotation( dx, dy );
	}
}


void MotionPath::Travel(	float* travel,
							int* pos,
							float* fraction )
{
	// fraction is a bit funny. It is the lerp value between 2 path locations,
	// so it isn't a constant distance.
	Vector2I vec;
	CalcDelta( *pos, *pos+1, &vec, 0 );

	float distBetween = 1.0f;
	if ( vec.x && vec.y ) {
		distBetween = 1.41f;
	}
	float distRemain = (1.0f-*fraction) * distBetween;

	if ( *travel >= distRemain ) {
		*travel -= distRemain;
		(*pos)++;
		*fraction = 0.0f;
	}
	else {
		*fraction += *travel / distBetween;
		*travel = 0.0f;
	}
}


void MotionPath::GetPos( int step, float fraction, float* x, float* z, float* rot )
{
	GLRELASSERT( step < pathLen );
	GLRELASSERT( fraction >= 0.0f && fraction < 1.0f );
	if ( step == pathLen-1 ) {
		step = pathLen-2;
		fraction = 1.0f;
	}
	Vector2<S16> path0 = GetPathAt( step );
	Vector2<S16> path1 = GetPathAt( step+1 );

	int dx = path1.x - path0.x;
	int dy = path1.y - path0.y;
	*x = (float)path0.x + fraction*(float)( dx );
	*z = (float)path0.y + fraction*(float)( dy );
	*rot = DeltaToRotation( dx, dy );
}


#if 0
// TEST CODE
void BattleScene::MouseMove( int x, int y )
{
	grinliz::Matrix4 mvpi;
	grinliz::Ray ray;
	Vector3F intersection;

	engine->CalcModelViewProjectionInverse( &mvpi );
	engine->RayFromScreen( x, y, mvpi, &ray );
	Model* model = engine->IntersectModel( ray, TEST_TRI, Model::MODEL_SELECTABLE, 0, &intersection );

	if ( model ) {
		Color4F color = { 1.0f, 0.0f, 0.0f, 1.0f };
		Color4F colorVel = { 0.0f, 0.0f, 0.0f, 0.0f };
		Vector3F vel = { 0, -0.5, 0 };

		game->particleSystem->Emit(	ParticleSystem::POINT,
									0, 1, ParticleSystem::PARTICLE_RAY,
									color,
									colorVel,
									intersection,
									0.0f,			// posFuzz
									vel,
									0.0f,			// velFuzz
									2000 );


		vel.y = 0;
		intersection.y = 0;
		color.Set( 0, 0, 1, 1 );

		game->particleSystem->Emit(	ParticleSystem::POINT,
									0, 1, ParticleSystem::PARTICLE_RAY,
									color,
									colorVel,
									intersection,
									0.0f,			// posFuzz
									vel,
									0.0f,			// velFuzz
									2000 );
	}	
}
#endif


const char* BattleScene::SelectionDesc()
{
	const char* result = "";
	if ( mapmaker_currentMapItem >= Map::NUM_ITEM_DEF ) {
		if ( mapmaker_currentMapItem == Map::NUM_ITEM_DEF )
			result = "smoke";
		else if ( mapmaker_currentMapItem == Map::NUM_ITEM_DEF+1 )
			result = "fire";
	}
	else {
		result = engine->GetMap()->GetItemDefName( mapmaker_currentMapItem );
	}
	return result;
}

void BattleScene::UpdatePreview()
{
	if ( mapmaker_preview ) {
		engine->FreeModel( mapmaker_preview );
		mapmaker_preview = 0;
	}
	if ( mapmaker_currentMapItem >= 0 ) {
		mapmaker_preview = engine->GetMap()->CreatePreview(	(int)mapmaker_mapSelection->X(), 
													(int)mapmaker_mapSelection->Z(), 
													mapmaker_currentMapItem, 
													(int)(mapmaker_mapSelection->GetRotation()/90.0f) );

		if ( mapmaker_preview ) {
			Texture* t = TextureManager::Instance()->GetTexture( "translucent" );
			mapmaker_preview->SetTexture( t );
			mapmaker_preview->SetTexXForm( 0, 0, TRANSLUCENT_WHITE, 0.0f );
		}
	}
}


void BattleScene::MouseMove( int x, int y )
{
	Vector2F window = { (float)x, (float)y };
	Vector2F view;
	engine->GetScreenport().WindowToView( window, &view );

	grinliz::Ray world;
	engine->GetScreenport().ViewToWorld( view, 0, &world );

	Vector3F p;
	int result = IntersectRayPlane( world.origin, world.direction, 1, 0.0f, &p );
	if ( result == grinliz::INTERSECT && p.x >= 0 && p.x < Map::SIZE && p.z >= 0 && p.z < Map::SIZE ) {
		int newX = (int)( p.x );
		int newZ = (int)( p.z );
		newX = Clamp( newX, 0, Map::SIZE-1 );
		newZ = Clamp( newZ, 0, Map::SIZE-1 );
		mapmaker_mapSelection->SetPos( (float)newX + 0.5f, 0.0f, (float)newZ + 0.5f );
	
		UpdatePreview();
	}
}


void BattleScene::SetLightMap( float r, float g, float b )
{
	int x = (int)mapmaker_mapSelection->X();
	int y = (int)mapmaker_mapSelection->Z();
	engine->GetMap()->SetLightMap0( x, y, r, g, b );
}


void BattleScene::RotateSelection( int delta )
{
	float rot = mapmaker_mapSelection->GetRotation() + 90.0f*(float)delta;
	mapmaker_mapSelection->SetRotation( rot );
	UpdatePreview();
}

void BattleScene::DeleteAtSelection()
{
	const Vector3F& pos = mapmaker_mapSelection->Pos();
	engine->GetMap()->DeleteAt( (int)pos.x, (int)pos.z );
	UpdatePreview();

	engine->GetMap()->SetPyro( (int)pos.x, (int)pos.z, 0, false );
}


void BattleScene::DeltaCurrentMapItem( int d )
{
	mapmaker_currentMapItem += d;
	static const int MAX = Map::NUM_ITEM_DEF + 2;	// placeholder for smoke and fire
	while ( mapmaker_currentMapItem < 0 ) { mapmaker_currentMapItem += MAX; }
	while ( mapmaker_currentMapItem >= MAX ) { mapmaker_currentMapItem -= MAX; }
	//if ( mapmaker_currentMapItem == 0 ) mapmaker_currentMapItem = 1;
	UpdatePreview();
}


