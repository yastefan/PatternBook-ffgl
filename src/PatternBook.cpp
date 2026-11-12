// SPDX-License-Identifier: GPL-3.0-or-later
#include "PatternBook.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "Shaders.h"

using namespace ffglex;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< PatternBook >,                         // Create method
	"PtBk",                                               // Plugin unique ID
	"PatternBook",                                        // Plugin name
	2,                                                    // API major version number
	1,                                                    // API minor version number
	PATTERNBOOK_VERSION_MAJOR,                            // Plugin major version number
	PATTERNBOOK_VERSION_MINOR,                            // Plugin minor version number
	FF_SOURCE,                                            // Plugin type
	"Minimal generative line patterns",                   // Plugin description
	"PatternBook - github.com/yastefan/PatternBook-ffgl"  // About
);

namespace
{
// Declared in menu order: Resolume shows parameters by ID and opens a new
// section whenever the group changes.
enum ParamID
{
	PID_PATTERN,
	PID_SEED,
	PID_SCALE,
	PID_FORM1,
	PID_FORM2,
	PID_FORM3,
	PID_WIDTH,
	PID_ROTATION,

	PID_PHASE1,
	PID_PHASE2,
	PID_DRIFT,
	PID_HIT,
	PID_HIT_AMOUNT,
	PID_HIT_EVERY,

	PID_DASH,
	PID_DASH_LENGTH,
	PID_DASH_SHIFT,
	PID_THIN,
	PID_MIRROR,
	PID_WARP,
	PID_WARP_SIZE,

	PID_INK_H,// hue, saturation, brightness and alpha in a row make one colour picker
	PID_INK_S,
	PID_INK_V,
	PID_INK_A,

	PID_PAPER_H,
	PID_PAPER_S,
	PID_PAPER_V,
	PID_PAPER_A,
};

// Shader families; the numbers must match FAMILY in Shaders.h.
enum Family
{
	FAMILY_TILES,
	FAMILY_RADIAL,
	FAMILY_CURVES,// drawn as geometry by kCurveVS; the family shader only lays the paper
	FAMILY_ISO,
	FAMILY_FIELD,
	FAMILY_WAVES,
};

// The Pattern menu, grouped by family. Each entry names the shader's family
// and variant, and what Form 1-3 and Phase 1-2 do in it (for the parameter
// display). Every pattern uses all five.
struct PatternEntry
{
	const char* name;
	int family;
	int variant;
	const char* form[ 3 ];
	const char* phase[ 2 ];
};
const PatternEntry kPatterns[] = {
	{ "Tiles: Arcs", FAMILY_TILES, 0, { "ribbon", "lines", "density" }, { "turns", "breath" } },
	{ "Tiles: Diagonals", FAMILY_TILES, 1, { "lines", "density", "direction" }, { "turns", "pulse" } },
	{ "Tiles: Hex Arcs", FAMILY_TILES, 2, { "ribbon", "lines", "density" }, { "turns", "breath" } },
	{ "Tiles: Squares", FAMILY_TILES, 3, { "size", "nested", "roundness" }, { "turn", "breath" } },

	{ "Radial: Spiral", FAMILY_RADIAL, 0, { "arms", "tightness", "hole" }, { "flow", "breath" } },
	{ "Radial: Broken Rings", FAMILY_RADIAL, 1, { "gaps", "segments", "speed spread" }, { "spin", "gap breath" } },
	{ "Radial: Rays", FAMILY_RADIAL, 2, { "inner radius", "count", "lengths" }, { "spin", "length pulse" } },
	{ "Radial: Polygons", FAMILY_RADIAL, 3, { "sides", "twist", "star / round" }, { "flow", "spin" } },
	{ "Radial: Rosette", FAMILY_RADIAL, 4, { "lobes", "depth", "bands" }, { "turn", "breath" } },

	{ "Curves: Chords", FAMILY_CURVES, 0, { "factor", "inner radius", "span" }, { "roll", "factor swing" } },
	{ "Curves: Lissajous", FAMILY_CURVES, 1, { "x frequency", "y frequency", "spread" }, { "shift", "spin" } },
	{ "Curves: Spirograph", FAMILY_CURVES, 2, { "lobes", "turns", "pen" }, { "weave", "spin" } },

	{ "Iso: Wire Cubes", FAMILY_ISO, 0, { "inner cube", "outline", "size" }, { "flips", "inner breath" } },
	{ "Iso: Hatched Cubes", FAMILY_ISO, 1, { "hatching", "top hatching", "size" }, { "slide", "size wave" } },
	{ "Iso: Lattice", FAMILY_ISO, 2, { "weave", "weave length", "families" }, { "glide", "weave travel" } },

	{ "Field: Flow Lines", FAMILY_FIELD, 0, { "turbulence", "feature size", "detail" }, { "evolve", "swirl" } },
	{ "Field: Contours", FAMILY_FIELD, 1, { "levels", "feature size", "detail" }, { "rise", "evolve" } },
	{ "Field: Terrain", FAMILY_FIELD, 2, { "height", "valley width", "view distance" }, { "flight", "camera sway" } },
	{ "Field: Circles", FAMILY_FIELD, 3, { "rings", "density", "size spread" }, { "ring flow", "drift" } },

	{ "Waves: Waves", FAMILY_WAVES, 0, { "frequency", "cross wave", "vertical change" }, { "travel", "drift" } },
	{ "Waves: Resonance", FAMILY_WAVES, 1, { "band width", "crossing", "left calm" }, { "drift", "band wander" } },
	{ "Waves: Bulge", FAMILY_WAVES, 2, { "strength", "lens size", "lenses" }, { "orbit", "strength breath" } },
	{ "Waves: Mesh", FAMILY_WAVES, 3, { "wavelength", "sources", "source distance" }, { "travel", "orbit" } },
	{ "Waves: Moire", FAMILY_WAVES, 4, { "ring ratio", "separation", "squareness" }, { "orbit", "ring flow" } },
	{ "Waves: Chladni", FAMILY_WAVES, 5, { "complexity", "levels", "warp" }, { "morph", "plate turn" } },
};
const int kPatternCount = sizeof( kPatterns ) / sizeof( kPatterns[ 0 ] );
const int kMaxSeed      = 99;

// Automatic hits; 0 means only the Hit button fires one.
const double kHitBars[]       = { 0.0, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0 };
const char* const kHitNames[] = { "Off", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
const int kHitCount           = 7;

const char* const kMirrorNames[] = { "Off", "Mirror X", "Mirror Y", "Mirror XY", "Kaleido 6", "Kaleido 8", "Kaleido 12" };
const int kMirrorCount           = 7;

const double kTau = 6.283185307179586;

// Mappings from the host's normalised 0..1 values to physical quantities.
float CellsValue( float v ) { return 2.0f + 46.0f * v * v; }// pattern cells across the frame height
float WidthPixels( float v ) { return 0.5f + 23.5f * v * v; }
float RotationRadians( float v ) { return ( v - 0.5f ) * 6.28318530718f; }
// Drift: Phase 1 cycles per bar (4/4), locked to the host tempo. 0 stops it,
// so a pattern with Drift and the Phases at 0 stands still; up to one cycle per beat.
float DriftPerBar( float v ) { return 4.0f * v * v; }
float DashLength( float v ) { return 0.02f + 0.6f * v * v; }// frame units (the height is 2)

// Displayed counts that the shader derives from Form 1 the same way.
int PolygonSides( float v ) { return 3 + static_cast< int >( v * 7.99f ); }  // RadialPolygons
int RosetteLobes( float v ) { return 3 + static_cast< int >( v * 21.99f ); } // RadialRosette

// Curves: everything is worked out here and handed to kCurveVS.
float ChordFactor( float v ) { return 10.0f * std::pow( v, 2.3219f ); }// 2 (a cardioid) at the middle
float ChordInner( float v ) { return 0.2f + 1.6f * v; }                 // 1 (the same circle) at the middle
int ChordLines( float cells ) { return std::min( std::max( static_cast< int >( std::lround( cells * 10.0f ) ), 20 ), 480 ); }
// x runs 1..6 and y 1..8, so the middle gives a 3 : 4 figure.
int LissajousFrequencyX( float v ) { return 1 + static_cast< int >( v * 5.99f ); }
int LissajousFrequencyY( float v ) { return 1 + static_cast< int >( v * 7.99f ); }
int LissajousCopies( float cells ) { return std::min( std::max( static_cast< int >( std::lround( cells * 0.6f ) ), 1 ), 32 ); }
int SpiroLobes( float v ) { return 3 + static_cast< int >( v * 13.99f ); }
int Gcd( int a, int b ) { return b == 0 ? a : Gcd( b, a % b ); }
// Turns before the curve closes; with a common factor it would close early
// and fewer lobes would show, so the next coprime count is used.
int SpiroTurns( float v, int lobes )
{
	int turns = std::min( 1 + static_cast< int >( v * 4.99f ), lobes - 1 );
	while( turns < lobes - 1 && Gcd( lobes, turns ) != 1 )
		++turns;
	return turns;
}
int SpiroCopies( float cells ) { return std::min( std::max( static_cast< int >( std::lround( cells * 0.6f ) ), 1 ), 16 ); }

int ClampIndex( float value, int count )
{
	return std::min( std::max( static_cast< int >( std::lround( value ) ), 0 ), count - 1 );
}

// Hue in [0, 1] (1 wraps to red), saturation and value in [0, 1].
void HsvToRgb( float h, float s, float v, float rgb[ 3 ] )
{
	h = ( h - std::floor( h ) ) * 6.0f;
	for( int i = 0; i < 3; ++i )
	{
		float k       = std::fmod( h + static_cast< float >( 5 - 2 * i ), 6.0f );
		float channel = std::min( std::max( std::min( k, 4.0f - k ), 0.0f ), 1.0f );
		rgb[ i ]      = v * ( 1.0f - s * channel );
	}
}

void SetColor( FFGLShader& shader, const char* name, const float hsva[ 4 ] )
{
	float rgb[ 3 ];
	HsvToRgb( hsva[ 0 ], hsva[ 1 ], hsva[ 2 ], rgb );
	float a = hsva[ 3 ];
	shader.Set( name, rgb[ 0 ] * a, rgb[ 1 ] * a, rgb[ 2 ] * a, a );// premultiplied
}

// A tileable value-noise landscape with four octaves, in -1..1. The lowest
// octave has 16 hills across the map, so the terrain repeats only every 16 hills.
const int kHeightMapSize = 512;
std::vector< float > MakeHeightMap()
{
	const int n = kHeightMapSize;
	std::vector< float > map( (size_t)n * n, 0.0f );
	uint32_t state = 0x9E3779B9u;
	auto random    = [ & ]() {
		state = state * 1664525u + 1013904223u;
		return static_cast< float >( state >> 8 ) / 16777216.0f - 0.5f;
	};
	float amp = 1.0f;
	for( int period = 16; period <= 128; period *= 2 )
	{
		std::vector< float > lattice( (size_t)period * period );
		for( float& v : lattice )
			v = random();
		float cell = static_cast< float >( n ) / period;
		for( int y = 0; y < n; ++y )
			for( int x = 0; x < n; ++x )
			{
				float fx = x / cell, fy = y / cell;
				int ix = static_cast< int >( fx ), iy = static_cast< int >( fy );
				float tx = fx - ix, ty = fy - iy;
				tx = tx * tx * ( 3.0f - 2.0f * tx );
				ty = ty * ty * ( 3.0f - 2.0f * ty );
				auto at = [ & ]( int i, int j ) { return lattice[ ( j % period ) * period + ( i % period ) ]; };
				float top    = at( ix, iy ) + ( at( ix + 1, iy ) - at( ix, iy ) ) * tx;
				float bottom = at( ix, iy + 1 ) + ( at( ix + 1, iy + 1 ) - at( ix, iy + 1 ) ) * tx;
				map[ (size_t)y * n + x ] += amp * ( top + ( bottom - top ) * ty );
			}
		amp *= 0.5f;
	}
	// Stretch to -1..1, so Form 1's height is the real height of the highest peak.
	float peak = 0.0f;
	for( float v : map )
		peak = std::max( peak, std::fabs( v ) );
	for( float& v : map )
		v /= peak;
	return map;
}

// Bilinear lookup in the height map with wrapping, as the GPU does it.
float SampleHeightMap( const std::vector< float >& map, double u, double v )
{
	const int n = kHeightMapSize;
	double x = u * n - 0.5, y = v * n - 0.5;
	double fx = std::floor( x ), fy = std::floor( y );
	float tx = static_cast< float >( x - fx ), ty = static_cast< float >( y - fy );
	auto at = [ & ]( double i, double j ) {
		int xi = static_cast< int >( std::fmod( std::fmod( i, n ) + n, n ) );
		int yi = static_cast< int >( std::fmod( std::fmod( j, n ) + n, n ) );
		return map[ (size_t)yi * n + xi ];
	};
	float top    = at( fx, fy ) + ( at( fx + 1, fy ) - at( fx, fy ) ) * tx;
	float bottom = at( fx, fy + 1 ) + ( at( fx + 1, fy + 1 ) - at( fx, fy + 1 ) ) * tx;
	return top + ( bottom - top ) * ty;
}

// Field: Terrain, matching FieldTerrain and TerrainHeight in Shaders.h.
const double kTerrainTile   = 16.0 / 0.55;// the height map repeats after this many units
float TerrainAmplitude( float v ) { return 0.1f + 4.0f * v * v; }
// Height of the valley floor right under the flight line at (x, z).
float TerrainFloor( const std::vector< float >& map, double x, double z, float amp, int seed )
{
	double u = x * 0.55 / 16.0 + seed * 0.37;
	double v = z * 0.55 / 16.0 + seed * 0.71;
	return amp * SampleHeightMap( map, u, v ) * 0.15f;
}

char gDisplay[ 40 ];
char* Display( const char* format, float value )
{
	snprintf( gDisplay, sizeof( gDisplay ), format, value );
	return gDisplay;
}
}// namespace

PatternBook::PatternBook()
{
	// The SDK leaves hostTime uninitialised until the host's first SetTime call.
	hostTime = 0.0;
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	SetOptionParamInfo( PID_PATTERN, "Pattern", kPatternCount, static_cast< float >( pattern ) );
	for( int i = 0; i < kPatternCount; ++i )
		SetParamElementInfo( PID_PATTERN, i, kPatterns[ i ].name, static_cast< float >( i ) );
	SetParamInfo( PID_SEED, "Seed", FF_TYPE_INTEGER, static_cast< float >( seed ) );
	SetParamRange( PID_SEED, 0.0f, static_cast< float >( kMaxSeed ) );
	SetParamInfo( PID_SCALE, "Scale", FF_TYPE_STANDARD, scale );
	SetParamInfo( PID_FORM1, "Form 1", FF_TYPE_STANDARD, forms[ 0 ] );
	SetParamInfo( PID_FORM2, "Form 2", FF_TYPE_STANDARD, forms[ 1 ] );
	SetParamInfo( PID_FORM3, "Form 3", FF_TYPE_STANDARD, forms[ 2 ] );
	SetParamInfo( PID_WIDTH, "Line Width", FF_TYPE_STANDARD, width );
	SetParamInfo( PID_ROTATION, "Rotation", FF_TYPE_STANDARD, rotation );
	for( unsigned int id = PID_PATTERN; id <= PID_ROTATION; ++id )
		SetParamGroup( id, "Pattern" );

	SetParamInfo( PID_PHASE1, "Phase 1", FF_TYPE_STANDARD, phases[ 0 ] );
	SetParamInfo( PID_PHASE2, "Phase 2", FF_TYPE_STANDARD, phases[ 1 ] );
	SetParamInfo( PID_DRIFT, "Drift", FF_TYPE_STANDARD, drift );
	SetParamInfo( PID_HIT, "Hit", FF_TYPE_EVENT, false );
	SetParamInfo( PID_HIT_AMOUNT, "Hit Amount", FF_TYPE_STANDARD, hitAmount );
	SetOptionParamInfo( PID_HIT_EVERY, "Hit Every", kHitCount, static_cast< float >( hitEvery ) );
	for( int i = 0; i < kHitCount; ++i )
		SetParamElementInfo( PID_HIT_EVERY, i, kHitNames[ i ], static_cast< float >( i ) );
	for( unsigned int id = PID_PHASE1; id <= PID_HIT_EVERY; ++id )
		SetParamGroup( id, "Animation" );

	SetParamInfo( PID_DASH, "Dash", FF_TYPE_STANDARD, dash );
	SetParamInfo( PID_DASH_LENGTH, "Dash Length", FF_TYPE_STANDARD, dashLength );
	SetParamInfo( PID_DASH_SHIFT, "Dash Shift", FF_TYPE_STANDARD, dashShift );
	SetParamInfo( PID_THIN, "Thin Out", FF_TYPE_STANDARD, thin );
	SetOptionParamInfo( PID_MIRROR, "Mirror", kMirrorCount, static_cast< float >( mirror ) );
	for( int i = 0; i < kMirrorCount; ++i )
		SetParamElementInfo( PID_MIRROR, i, kMirrorNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PID_WARP, "Warp", FF_TYPE_STANDARD, warp );
	SetParamInfo( PID_WARP_SIZE, "Warp Size", FF_TYPE_STANDARD, warpSize );
	for( unsigned int id = PID_DASH; id <= PID_WARP_SIZE; ++id )
		SetParamGroup( id, "Lines" );

	// Resolume shows hue, saturation, brightness and alpha in a row as one
	// colour picker (named as in the FFGL SDK's example).
	SetParamInfo( PID_INK_H, "Ink", FF_TYPE_HUE, ink[ 0 ] );
	SetParamInfo( PID_INK_S, "Ink_Sat", FF_TYPE_SATURATION, ink[ 1 ] );
	SetParamInfo( PID_INK_V, "Ink_Bri", FF_TYPE_BRIGHTNESS, ink[ 2 ] );
	SetParamInfo( PID_INK_A, "Ink_Alpha", FF_TYPE_ALPHA, ink[ 3 ] );
	for( unsigned int id = PID_INK_H; id <= PID_INK_A; ++id )
		SetParamGroup( id, "Ink" );
	SetParamInfo( PID_PAPER_H, "Paper", FF_TYPE_HUE, paper[ 0 ] );
	SetParamInfo( PID_PAPER_S, "Paper_Sat", FF_TYPE_SATURATION, paper[ 1 ] );
	SetParamInfo( PID_PAPER_V, "Paper_Bri", FF_TYPE_BRIGHTNESS, paper[ 2 ] );
	SetParamInfo( PID_PAPER_A, "Paper_Alpha", FF_TYPE_ALPHA, paper[ 3 ] );
	for( unsigned int id = PID_PAPER_H; id <= PID_PAPER_A; ++id )
		SetParamGroup( id, "Paper" );
}

FFResult PatternBook::InitGL( const FFGLViewportStruct* vp )
{
	// The host can hand us a context with its own error still pending (Resolume
	// does on the first instantiation). Drain it so we only see our own errors.
	while( glGetError() != GL_NO_ERROR )
	{
	}

	// One program per family keeps each shader small, which keeps the cheap
	// patterns fast on integrated GPUs.
	for( int f = 0; f < kFamilyCount; ++f )
	{
		std::string source = "#version 410 core\n#define FAMILY " + std::to_string( f ) + "\n" + pattern::kPatternFS;
		if( !shaders[ f ].Compile( pattern::kFullscreenVS, source.c_str() ) )
		{
			FFGLLog::LogToHost( "PatternBook: shader failed to compile" );
			DeInitGL();
			return FF_FAIL;
		}
	}
	if( !curveShader.Compile( pattern::kCurveVS, pattern::kCurveFS ) )
	{
		FFGLLog::LogToHost( "PatternBook: curve shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenVertexArrays( 1, &emptyVAO );

	heightValues = MakeHeightMap();
	glGenTextures( 1, &heightMap );
	{
		Scoped2DTextureBinding textureBinding( heightMap );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R32F, kHeightMapSize, kHeightMapSize, 0, GL_RED, GL_FLOAT, heightValues.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult PatternBook::DeInitGL()
{
	for( FFGLShader& shader : shaders )
		shader.FreeGLResources();
	curveShader.FreeGLResources();
	if( emptyVAO )
		glDeleteVertexArrays( 1, &emptyVAO );
	emptyVAO = 0;
	if( heightMap )
		glDeleteTextures( 1, &heightMap );
	heightMap = 0;
	return FF_SUCCESS;
}

void PatternBook::UpdateClock( float deltaTime )
{
	double before = barPosition;
	// Resolume sends the tempo and the phase within the bar every frame. Until
	// it does (or in a host without beat info), run our own clock at its tempo.
	if( std::isfinite( barPhase ) && lastBarPhase >= 0.0f && barPhase != lastBarPhase )
		hostSendsBeats = true;
	if( hostSendsBeats && std::isfinite( barPhase ) )
	{
		double bar = std::floor( barPosition );
		if( barPhase < lastBarPhase - 0.5f )
			bar += 1.0;
		barPosition = bar + barPhase;
	}
	else
		barPosition += deltaTime * std::max( bpm, 1.0f ) / 240.0;
	lastBarPhase = barPhase;

	// Drift adds to Phase 1. Accumulating (rather than multiplying the bar
	// position) lets Drift change the pace without making the pattern jump.
	double cycles = std::max( barPosition - before, 0.0 ) * DriftPerBar( drift );
	drifted       = drifted + cycles - std::floor( drifted + cycles );
	// The terrain camera flies one height-map tile per cycle, towards where Rotation points.
	double heading = RotationRadians( rotation );
	flyX           = std::fmod( flyX + kTerrainTile * cycles * std::sin( heading ), 64.0 * kTerrainTile );
	flyZ           = std::fmod( flyZ + kTerrainTile * cycles * std::cos( heading ), 64.0 * kTerrainTile );

	bool hit   = hitPending;
	hitPending = false;
	double interval = kHitBars[ hitEvery ];
	if( interval > 0.0 && std::floor( barPosition / interval + 1e-6 ) != std::floor( before / interval + 1e-6 ) )
		hit = true;
	hitAge = std::min( hitAge + deltaTime, 1000.0 );
	if( hit )
	{
		hitCount = std::fmod( hitCount + 1.0, 3072.0 );// a multiple of every turn count the shader uses
		hitAge   = 0.0;
	}
}

float PatternBook::Phase1() const
{
	double p = phases[ 0 ] + drifted;
	return static_cast< float >( p - std::floor( p ) );
}

void PatternBook::UpdateCamera( float deltaTime )
{
	if( heightValues.empty() )
		return;
	// Keep clear of the valley floor here and a little ahead, and glide so the
	// camera rises and dips smoothly. Phase 1 moves the camera on along the
	// flight line by up to one tile (where the landscape repeats).
	float amp      = TerrainAmplitude( forms[ 0 ] );
	double heading = RotationRadians( rotation );
	double along   = phases[ 0 ] * kTerrainTile;
	double x = flyX + along * std::sin( heading ), z = flyZ + along * std::cos( heading );
	float target = -1e9f;
	for( double ahead : { 0.0, 1.0, 2.5 } )
		target = std::max( target, TerrainFloor( heightValues, x + ahead * std::sin( heading ), z + ahead * std::cos( heading ), amp, seed ) );
	target += 0.45f;
	if( !cameraReady )
		cameraHeight = target;
	else
		cameraHeight += ( target - cameraHeight ) * ( 1.0f - std::exp( -deltaTime / 0.35f ) );
	cameraReady = true;
}

FFResult PatternBook::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	const PatternEntry& entry = kPatterns[ pattern ];
	FFGLShader& shader        = shaders[ entry.family ];
	if( !shader.IsReady() )
		return FF_FAIL;

	float deltaTime = 0.0f;
	if( lastTime >= 0.0 )
		deltaTime = static_cast< float >( ( hostTime - lastTime ) / 1000.0 );
	lastTime = hostTime;
	// A retriggered clip or a stalled host must not make everything jump.
	if( !std::isfinite( deltaTime ) )
		deltaTime = 0.0f;
	deltaTime = std::min( std::max( deltaTime, 0.0f ), 0.1f );

	UpdateClock( deltaTime );
	if( entry.family == FAMILY_FIELD && entry.variant == 2 )
		UpdateCamera( deltaTime );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "uViewport", static_cast< float >( currentViewport.x ), static_cast< float >( currentViewport.y ),
				static_cast< float >( currentViewport.width ), static_cast< float >( currentViewport.height ) );
	shader.Set( "uVariant", entry.variant );
	shader.Set( "uSeed", static_cast< float >( seed ) );
	shader.Set( "uCells", CellsValue( scale ) );
	shader.Set( "uForm1", forms[ 0 ] );
	shader.Set( "uForm2", forms[ 1 ] );
	shader.Set( "uForm3", forms[ 2 ] );
	shader.Set( "uWidth", WidthPixels( width ) );
	shader.Set( "uRotation", RotationRadians( rotation ) );
	shader.Set( "uPhase1", Phase1() );
	shader.Set( "uPhase2", phases[ 1 ] );
	shader.Set( "uHitCount", static_cast< float >( hitCount ) );
	shader.Set( "uHitAge", static_cast< float >( hitAge ) );
	shader.Set( "uHitAmount", hitAmount );
	shader.Set( "uDash", dash );
	shader.Set( "uDashLength", DashLength( dashLength ) );
	shader.Set( "uDashShift", dashShift );
	shader.Set( "uThin", thin * 0.95f );
	shader.Set( "uMirror", mirror );
	shader.Set( "uWarp", warp );
	shader.Set( "uWarpSize", warpSize );
	{
		double heading = RotationRadians( rotation );
		double along   = phases[ 0 ] * kTerrainTile;
		shader.Set( "uCamera", static_cast< float >( flyX + along * std::sin( heading ) ), cameraHeight,
					static_cast< float >( flyZ + along * std::cos( heading ) ) );
	}
	SetColor( shader, "uInk", ink );
	SetColor( shader, "uPaper", paper );

	ScopedSamplerActivation samplerActivation( 0 );
	Scoped2DTextureBinding textureBinding( heightMap );
	shader.Set( "uHeightMap", 0 );

	ScopedVAOBinding vaoBinding( emptyVAO );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	if( entry.family == FAMILY_CURVES )
		DrawCurves();
	return FF_SUCCESS;
}

void PatternBook::DrawCurves()
{
	const PatternEntry& entry = kPatterns[ pattern ];
	float cells   = CellsValue( scale );
	float p1      = Phase1();
	float p2      = phases[ 1 ];
	float kick    = hitCount > 0.5 ? static_cast< float >( 2.0 * hitAmount * std::exp( -1.5 * hitAge ) ) : 0.0f;
	int lines     = 1;// chords, or copies of a closed curve
	int segments  = 1;// segments per line
	float length  = 1.0f;// rough length of one line, for even dashes

	ScopedShaderBinding shaderBinding( curveShader.GetGLID() );
	curveShader.Set( "uCurve", entry.variant );
	if( entry.variant == 0 )
	{
		// Chords: point i at angle a on the outer circle joins k * a on an inner
		// circle. Phase 1 rolls the inner circle round, Phase 2 swings the factor.
		lines = ChordLines( cells );
		curveShader.Set( "uFactor", ChordFactor( forms[ 0 ] ) + static_cast< float >( std::sin( kTau * p2 ) ) + kick );
		curveShader.Set( "uInner", ChordInner( forms[ 1 ] ) );
		// Form 3: below the middle the chords start from part of the outer
		// circle only; above it the inner circle is turned against the outer one.
		curveShader.Set( "uSpan", 0.15f + 0.85f * std::min( forms[ 2 ] * 2.0f, 1.0f ) );
		curveShader.Set( "uTwist", std::max( forms[ 2 ] * 2.0f - 1.0f, 0.0f ) * 3.14159265f );
		curveShader.Set( "uShift", static_cast< float >( kTau * p1 ) );
	}
	else if( entry.variant == 1 )
	{
		// Lissajous: copies of one figure, each a little further on in phase.
		// Phase 1 shifts the phase (the figure tumbles), Phase 2 spins it in depth.
		lines    = LissajousCopies( cells );
		segments = 240;
		length   = 4.0f;
		curveShader.Set( "uFreq", static_cast< float >( LissajousFrequencyX( forms[ 0 ] ) ), static_cast< float >( LissajousFrequencyY( forms[ 1 ] ) ) );
		curveShader.Set( "uSpread", forms[ 2 ] * 0.35f );// phase step between copies
		curveShader.Set( "uShift", static_cast< float >( kTau * p1 ) + kick );
		curveShader.Set( "uSpin", static_cast< float >( kTau * p2 ) );
	}
	else
	{
		// Spirograph: a wheel rolling inside a ring; copies turned between the
		// lobes give the guilloche look. Phase 1 turns every other copy against
		// the rest by one lobe, Phase 2 spins the figure.
		int lobes = SpiroLobes( forms[ 0 ] );
		int turns = SpiroTurns( forms[ 1 ], lobes );
		lines     = SpiroCopies( cells );
		segments  = ( 64 + 24 * lobes ) * turns;
		length    = 6.0f * turns;
		float r   = static_cast< float >( turns ) / lobes;// wheel radius, ring radius 1
		float pen = r * ( 0.2f + 1.4f * forms[ 2 ] );
		curveShader.Set( "uWheel", r, pen, static_cast< float >( turns ) );
		curveShader.Set( "uLobes", static_cast< float >( lobes ) );
		curveShader.Set( "uShift", static_cast< float >( kTau * p1 ) + kick );
		curveShader.Set( "uSpin", static_cast< float >( kTau * p2 ) );
		curveShader.Set( "uSize", 0.85f / ( 1.0f - r + pen ) );
	}

	curveShader.Set( "uViewport", static_cast< float >( currentViewport.x ), static_cast< float >( currentViewport.y ),
					 static_cast< float >( currentViewport.width ), static_cast< float >( currentViewport.height ) );
	curveShader.Set( "uLines", static_cast< float >( lines ) );
	curveShader.Set( "uSegments", static_cast< float >( segments ) );
	curveShader.Set( "uLength", length );
	curveShader.Set( "uRotation", RotationRadians( rotation ) );
	curveShader.Set( "uWidth", WidthPixels( width ) );
	curveShader.Set( "uMirror", mirror );
	curveShader.Set( "uDash", dash );
	curveShader.Set( "uDashLength", DashLength( dashLength ) );
	curveShader.Set( "uDashShift", dashShift );
	curveShader.Set( "uThin", thin * 0.95f );
	curveShader.Set( "uSeed", static_cast< float >( seed ) );
	SetColor( curveShader, "uInk", ink );

	// Mirror draws reflected and turned copies of the figure.
	static const int kCopies[ kMirrorCount ] = { 1, 2, 2, 4, 6, 8, 12 };

	// Premultiplied "over" blending, restored to the host's default afterwards.
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glDrawArraysInstanced( GL_TRIANGLES, 0, lines * segments * 6, kCopies[ mirror ] );
	glBlendFunc( GL_ONE, GL_ZERO );
	glDisable( GL_BLEND );
}

FFResult PatternBook::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_INK_H && index <= PID_INK_A )
	{
		ink[ index - PID_INK_H ] = value;
		return FF_SUCCESS;
	}
	if( index >= PID_PAPER_H && index <= PID_PAPER_A )
	{
		paper[ index - PID_PAPER_H ] = value;
		return FF_SUCCESS;
	}

	switch( index )
	{
	case PID_PATTERN: pattern = ClampIndex( value, kPatternCount ); break;
	case PID_SEED: seed = ClampIndex( value, kMaxSeed + 1 ); break;
	case PID_SCALE: scale = value; break;
	case PID_FORM1: forms[ 0 ] = value; break;
	case PID_FORM2: forms[ 1 ] = value; break;
	case PID_FORM3: forms[ 2 ] = value; break;
	case PID_WIDTH: width = value; break;
	case PID_ROTATION: rotation = value; break;
	case PID_PHASE1: phases[ 0 ] = value; break;
	case PID_PHASE2: phases[ 1 ] = value; break;
	case PID_DRIFT: drift = value; break;
	case PID_HIT:
		if( value > 0.5f && !hitPressed )
			hitPending = true;
		hitPressed = value > 0.5f;
		break;
	case PID_HIT_AMOUNT: hitAmount = value; break;
	case PID_HIT_EVERY: hitEvery = ClampIndex( value, kHitCount ); break;
	case PID_DASH: dash = value; break;
	case PID_DASH_LENGTH: dashLength = value; break;
	case PID_DASH_SHIFT: dashShift = value; break;
	case PID_THIN: thin = value; break;
	case PID_MIRROR: mirror = ClampIndex( value, kMirrorCount ); break;
	case PID_WARP: warp = value; break;
	case PID_WARP_SIZE: warpSize = value; break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

float PatternBook::GetFloatParameter( unsigned int index )
{
	if( index >= PID_INK_H && index <= PID_INK_A )
		return ink[ index - PID_INK_H ];
	if( index >= PID_PAPER_H && index <= PID_PAPER_A )
		return paper[ index - PID_PAPER_H ];

	switch( index )
	{
	case PID_PATTERN: return static_cast< float >( pattern );
	case PID_SEED: return static_cast< float >( seed );
	case PID_SCALE: return scale;
	case PID_FORM1: return forms[ 0 ];
	case PID_FORM2: return forms[ 1 ];
	case PID_FORM3: return forms[ 2 ];
	case PID_WIDTH: return width;
	case PID_ROTATION: return rotation;
	case PID_PHASE1: return phases[ 0 ];
	case PID_PHASE2: return phases[ 1 ];
	case PID_DRIFT: return drift;
	case PID_HIT: return hitPressed ? 1.0f : 0.0f;
	case PID_HIT_AMOUNT: return hitAmount;
	case PID_HIT_EVERY: return static_cast< float >( hitEvery );
	case PID_DASH: return dash;
	case PID_DASH_LENGTH: return dashLength;
	case PID_DASH_SHIFT: return dashShift;
	case PID_THIN: return thin;
	case PID_MIRROR: return static_cast< float >( mirror );
	case PID_WARP: return warp;
	case PID_WARP_SIZE: return warpSize;
	default: return 0.0f;
	}
}

char* PatternBook::GetParameterDisplay( unsigned int index )
{
	const PatternEntry& entry = kPatterns[ pattern ];
	bool curves               = entry.family == FAMILY_CURVES;
	switch( index )
	{
	case PID_SCALE:
		if( curves && entry.variant == 0 )
			return Display( "%.0f chords", static_cast< float >( ChordLines( CellsValue( scale ) ) ) );
		if( curves && entry.variant == 1 )
			return Display( "%.0f copies", static_cast< float >( LissajousCopies( CellsValue( scale ) ) ) );
		if( curves )
			return Display( "%.0f copies", static_cast< float >( SpiroCopies( CellsValue( scale ) ) ) );
		return Display( "%.1f cells", CellsValue( scale ) );
	case PID_FORM1:
	case PID_FORM2:
	case PID_FORM3:
	{
		int f   = static_cast< int >( index - PID_FORM1 );
		float v = forms[ f ];
		if( entry.family == FAMILY_RADIAL && entry.variant == 3 && f == 0 )
			return Display( "%.0f sides", static_cast< float >( PolygonSides( v ) ) );
		if( entry.family == FAMILY_RADIAL && entry.variant == 4 && f == 0 )
			return Display( "%.0f lobes", static_cast< float >( RosetteLobes( v ) ) );
		if( curves && entry.variant == 0 && f == 0 )
			return Display( "factor %.2f", ChordFactor( v ) );
		if( curves && entry.variant == 0 && f == 1 )
			return Display( "inner %.2f", ChordInner( v ) );
		if( curves && entry.variant == 1 && f < 2 )
			return f == 0 ? Display( "x frequency %.0f", static_cast< float >( LissajousFrequencyX( v ) ) ) : Display( "y frequency %.0f", static_cast< float >( LissajousFrequencyY( v ) ) );
		if( curves && entry.variant == 2 && f == 0 )
			return Display( "%.0f lobes", static_cast< float >( SpiroLobes( v ) ) );
		if( curves && entry.variant == 2 && f == 1 )
			return Display( "%.0f turns", static_cast< float >( SpiroTurns( v, SpiroLobes( forms[ 0 ] ) ) ) );
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", entry.form[ f ], v * 100.0f );
		return gDisplay;
	}
	case PID_PHASE1:
	case PID_PHASE2:
	{
		int ph = static_cast< int >( index - PID_PHASE1 );
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", entry.phase[ ph ], phases[ ph ] * 100.0f );
		return gDisplay;
	}
	case PID_DRIFT:
	{
		float speed = DriftPerBar( drift );
		if( speed < 0.005f )
			return const_cast< char* >( "stopped" );
		if( speed >= 1.0f )
			snprintf( gDisplay, sizeof( gDisplay ), "%.1f per bar", speed );
		else
			snprintf( gDisplay, sizeof( gDisplay ), "1 per %.1f bars", 1.0f / speed );
		return gDisplay;
	}
	case PID_WIDTH: return Display( "%.1f px", WidthPixels( width ) );
	case PID_ROTATION: return Display( "%.0f deg", RotationRadians( rotation ) * 57.2957795f );
	case PID_HIT_AMOUNT: return Display( "%.0f%%", hitAmount * 100.0f );
	case PID_DASH: return dash <= 0.0f ? const_cast< char* >( "solid" ) : Display( "gaps %.0f%%", dash * 90.0f );
	case PID_DASH_LENGTH: return Display( "%.2f", DashLength( dashLength ) );
	case PID_DASH_SHIFT: return Display( "%.0f%%", dashShift * 100.0f );
	case PID_THIN: return Display( "%.0f%% removed", thin * 95.0f );
	case PID_WARP: return Display( "%.0f%%", warp * 100.0f );
	case PID_WARP_SIZE: return Display( "%.0f%%", warpSize * 100.0f );
	default: return CFFGLPlugin::GetParameterDisplay( index );
	}
}
