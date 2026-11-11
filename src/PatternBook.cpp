// SPDX-License-Identifier: GPL-3.0-or-later
#include "PatternBook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
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
	"Minimal generative patterns, animated to the beat",  // Plugin description
	"PatternBook - github.com/yastefan/PatternBook-ffgl"  // About
);

namespace
{
// Declared in menu order: Resolume shows parameters by ID and opens a new
// section whenever the group changes. Once released, append only.
enum ParamID
{
	PID_PATTERN,
	PID_SEED,
	PID_SCALE,
	PID_FORM,
	PID_FORM2,
	PID_FORM3,
	PID_WIDTH,
	PID_ROTATION,

	PID_SPEED,
	PID_AMOUNT,
	PID_MOTION1,
	PID_MOTION2,
	PID_MOTION3,
	PID_LOOP,
	PID_HIT,
	PID_HIT_AMOUNT,
	PID_HIT_EVERY,

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
	FAMILY_GRID,
	FAMILY_RADIAL,
	FAMILY_ISO,
	FAMILY_DISTORT,
	FAMILY_NOISE,
};

// The Pattern menu, grouped by family. Each entry names the shader's family
// and variant, and what Form 1-3 and Motion 1-3 do in it (for the parameter
// display). Every pattern uses all six.
struct PatternEntry
{
	const char* name;
	int family;
	int variant;
	const char* form;
	const char* form2;
	const char* form3;
	const char* motion[ 3 ];
};
const PatternEntry kPatterns[] = {
	{ "Grid: Arcs", FAMILY_GRID, 0, "ribbon", "lines", "density", { "turns", "ribbon breath", "drift" } },
	{ "Grid: Diagonals", FAMILY_GRID, 1, "lines", "density", "direction", { "turns", "drift", "line pulse" } },
	{ "Grid: Squares", FAMILY_GRID, 2, "size", "nested", "roundness", { "turning", "size breath", "drift" } },
	{ "Grid: Dots", FAMILY_GRID, 3, "dot size", "wavelength", "ring", { "breathing", "drift", "spin" } },

	{ "Radial: Spiral", FAMILY_RADIAL, 0, "arms", "tightness", "hole", { "spin", "breath", "arm waves" } },
	{ "Radial: Broken Rings", FAMILY_RADIAL, 1, "gaps", "segments", "speed spread", { "spin", "gap breath", "ring ripple" } },
	{ "Radial: Rays", FAMILY_RADIAL, 2, "inner radius", "count", "lengths", { "rays", "bend", "length pulse" } },
	{ "Radial: Dot Rings", FAMILY_RADIAL, 3, "dot size", "density", "slide", { "slide", "dot pulse", "ring waves" } },
	{ "Radial: Chords", FAMILY_RADIAL, 4, "factor", "inner radius", "span", { "roll", "factor swing", "inner breath" } },

	{ "Iso: Cubes", FAMILY_ISO, 0, "scatter", "contrast", "size", { "size wave", "light", "drift" } },
	{ "Iso: Wire Cubes", FAMILY_ISO, 1, "inner cube", "outline", "size", { "flips", "inner breath", "drift" } },
	{ "Iso: Hatched Cubes", FAMILY_ISO, 2, "hatching", "top hatching", "size", { "size wave", "hatch slide", "drift" } },
	{ "Iso: Noise Heights", FAMILY_ISO, 4, "height", "feature size", "terraces", { "drift", "surge", "rain" } },
	{ "Iso: Lattice", FAMILY_ISO, 3, "weave", "weave length", "families", { "glide", "weave travel", "spin" } },

	{ "Noise: Flow Lines", FAMILY_NOISE, 0, "turbulence", "feature size", "detail", { "evolve", "flow", "vortex" } },
	{ "Noise: Contours", FAMILY_NOISE, 1, "levels", "feature size", "detail", { "evolve", "rise", "drift" } },
	{ "Noise: Terrain", FAMILY_NOISE, 2, "height", "valley width", "view distance", { "flight", "waves", "camera sway" } },
	{ "Noise: Circles", FAMILY_NOISE, 3, "rings", "density", "size spread", { "drift", "ring flow", "spin" } },

	{ "Distort: Interference", FAMILY_DISTORT, 0, "wavelength", "sources", "source distance", { "waves", "wavelength breath", "drift" } },
	{ "Distort: Ripple", FAMILY_DISTORT, 1, "wavelength", "falloff", "sharpness", { "ripples", "wander", "drift" } },
	{ "Distort: Wave Lines", FAMILY_DISTORT, 2, "frequency", "cross wave", "vertical change", { "waves", "drift", "swell" } },
	{ "Distort: Resonance", FAMILY_DISTORT, 5, "band width", "crossing", "left calm", { "drift", "band wander", "crossing pulse" } },
	{ "Distort: Moire Rings", FAMILY_DISTORT, 3, "spacing", "ring ratio", "orbit", { "orbit", "ring flow", "spacing breath" } },
	{ "Distort: Chladni", FAMILY_DISTORT, 4, "complexity", "levels", "warp", { "morph", "plate spin", "warp wobble" } },
};
const int kPatternCount = sizeof( kPatterns ) / sizeof( kPatterns[ 0 ] );
const int kMaxSeed      = 99;

// Automatic hits; 0 means only the Hit button fires one.
const double kHitBars[]       = { 0.0, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0 };
const char* const kLoopNames[] = { "Endless", "Sway" };
const char* const kHitNames[] = { "Off", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
const int kHitCount           = 7;

// The shader's cycle counters wrap here: a multiple of 4 and 6, so quarter
// turns and the wire cubes' sixth turns land where they started.
const double kWrap = 3072.0;

// Mappings from the host's normalised 0..1 values to physical quantities.
float CellsValue( float v ) { return 2.0f + 46.0f * v * v; }// pattern cells across the frame height
float WidthPixels( float v ) { return 0.5f + 23.5f * v * v; }
float RotationRadians( float v ) { return ( v - 0.5f ) * 6.28318530718f; }
// Animation cycles per bar (4/4), locked to the host tempo: 0 stops, 4 is one per beat.
float CyclesPerBar( float v ) { return 4.0f * v * v; }

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

// A tileable value-noise landscape with four octaves, in -1..1.
// The lowest octave has 16 hills across the map, so the terrain repeats
// only every 16 hills.
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
	float amp = 1.0f, total = 0.0f;
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
		total += amp;
		amp *= 0.5f;
	}
	// Stretch to -1..1, so Form's height is the real height of the highest peak.
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

// Radial: Chords, drawn by kChordVS in Shaders.h.
float ChordFactor( float v ) { return 10.0f * std::pow( v, 2.3219f ); }// 2 (a cardioid) at the middle
float ChordInner( float v ) { return 0.2f + 1.6f * v; }                 // 1 (the same circle) at the middle
int ChordLines( float cells ) { return std::min( std::max( static_cast< int >( std::lround( cells * 10.0f ) ), 20 ), 480 ); }

float TerrainAmplitude( float form ) { return 0.1f + 4.0f * form * form; }// as in NoiseTerrain

// Height of the valley floor right under the flight line at (x, z); matches
// TerrainHeight in Shaders.h at zero sideways distance.
float TerrainFloor( const std::vector< float >& map, double x, double z, float amp, int seed )
{
	double u = x * 0.55 / 16.0 + seed * 0.37;
	double v = z * 0.55 / 16.0 + seed * 0.71;
	return amp * SampleHeightMap( map, u, v ) * 0.15f;
}

char gDisplay[ 32 ];
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
	SetParamInfo( PID_FORM, "Form 1", FF_TYPE_STANDARD, form );
	SetParamInfo( PID_FORM2, "Form 2", FF_TYPE_STANDARD, form2 );
	SetParamInfo( PID_FORM3, "Form 3", FF_TYPE_STANDARD, form3 );
	SetParamInfo( PID_WIDTH, "Line Width", FF_TYPE_STANDARD, width );
	SetParamInfo( PID_ROTATION, "Rotation", FF_TYPE_STANDARD, rotation );
	for( unsigned int id = PID_PATTERN; id <= PID_ROTATION; ++id )
		SetParamGroup( id, "Pattern" );

	SetParamInfo( PID_SPEED, "Speed", FF_TYPE_STANDARD, speed );
	SetParamInfo( PID_AMOUNT, "Amount", FF_TYPE_STANDARD, amount );
	SetParamInfo( PID_MOTION1, "Motion 1", FF_TYPE_STANDARD, motions[ 0 ] );
	SetParamInfo( PID_MOTION2, "Motion 2", FF_TYPE_STANDARD, motions[ 1 ] );
	SetParamInfo( PID_MOTION3, "Motion 3", FF_TYPE_STANDARD, motions[ 2 ] );
	SetOptionParamInfo( PID_LOOP, "Loop", 2, static_cast< float >( loopMode ) );
	for( int i = 0; i < 2; ++i )
		SetParamElementInfo( PID_LOOP, i, kLoopNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PID_HIT, "Hit", FF_TYPE_EVENT, false );
	SetParamInfo( PID_HIT_AMOUNT, "Hit Amount", FF_TYPE_STANDARD, hitAmount );
	SetOptionParamInfo( PID_HIT_EVERY, "Hit Every", kHitCount, static_cast< float >( hitEvery ) );
	for( int i = 0; i < kHitCount; ++i )
		SetParamElementInfo( PID_HIT_EVERY, i, kHitNames[ i ], static_cast< float >( i ) );
	for( unsigned int id = PID_SPEED; id <= PID_HIT_EVERY; ++id )
		SetParamGroup( id, "Animation" );

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
	if( !chordShader.Compile( pattern::kChordVS, pattern::kChordFS ) )
	{
		FFGLLog::LogToHost( "PatternBook: chord shader failed to compile" );
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
	chordShader.FreeGLResources();
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

	// Accumulate rather than multiply the bar position, so moving Speed or
	// Amount changes the pace without making the pattern jump.
	double bars   = std::max( barPosition - before, 0.0 );
	double cycles = bars * CyclesPerBar( speed );
	phase         = std::fmod( phase + cycles, kWrap );
	for( int m = 0; m < 3; ++m )
		travel[ m ] = std::fmod( travel[ m ] + cycles * amount * motions[ m ], kWrap );
	double moved = cycles * amount * motions[ 0 ];
	// The terrain camera flies 3 units per cycle towards where Rotation points (Motion 1).
	double heading = RotationRadians( rotation );
	flyX           = std::fmod( flyX + 3.0 * moved * std::sin( heading ), 4096.0 );
	flyZ           = std::fmod( flyZ + 3.0 * moved * std::cos( heading ), 4096.0 );

	bool hit   = hitPending;
	hitPending = false;
	double interval = kHitBars[ hitEvery ];
	if( interval > 0.0 && std::floor( barPosition / interval + 1e-6 ) != std::floor( before / interval + 1e-6 ) )
		hit = true;
	hitAge = std::min( hitAge + deltaTime, 1000.0 );
	if( hit )
	{
		hitCount = std::fmod( hitCount + 1.0, kWrap );
		hitAge   = 0.0;
	}
}

void PatternBook::UpdateCamera( float deltaTime )
{
	if( heightValues.empty() )
		return;
	// Keep clear of the valley floor here and a little ahead, and glide so the
	// camera rises and dips smoothly.
	float amp      = TerrainAmplitude( form );
	double heading = RotationRadians( rotation );
	float target   = -1e9f;
	for( double ahead : { 0.0, 1.0, 2.5 } )
		target = std::max( target, TerrainFloor( heightValues, flyX + ahead * std::sin( heading ), flyZ + ahead * std::cos( heading ), amp, seed ) );
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
	UpdateCamera( deltaTime );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "uViewport", static_cast< float >( currentViewport.x ), static_cast< float >( currentViewport.y ),
				static_cast< float >( currentViewport.width ), static_cast< float >( currentViewport.height ) );
	shader.Set( "uVariant", entry.variant );
	shader.Set( "uSeed", static_cast< float >( seed ) );
	shader.Set( "uCells", CellsValue( scale ) );
	shader.Set( "uForm", form );
	shader.Set( "uForm2", form2 );
	shader.Set( "uForm3", form3 );
	shader.Set( "uWidth", WidthPixels( width ) );
	shader.Set( "uRotation", RotationRadians( rotation ) );
	shader.Set( "uPhase", static_cast< float >( phase ) );
	// Motion 1 drives what each pattern did before (uMotion, uAmount); Motion 2
	// and 3 drive extra movements, each as a strength and a running travel.
	shader.Set( "uMotion", static_cast< float >( travel[ 0 ] ) );
	shader.Set( "uAmount", amount * motions[ 0 ] );
	shader.Set( "uStrength2", amount * motions[ 1 ] );
	shader.Set( "uStrength3", amount * motions[ 2 ] );
	shader.Set( "uTravel2", static_cast< float >( travel[ 1 ] ) );
	shader.Set( "uTravel3", static_cast< float >( travel[ 2 ] ) );
	shader.Set( "uSway", loopMode );
	shader.Set( "uHitCount", static_cast< float >( hitCount ) );
	shader.Set( "uHitAge", static_cast< float >( hitAge ) );
	shader.Set( "uHitAmount", hitAmount );
	shader.Set( "uCamera", static_cast< float >( flyX ), cameraHeight, static_cast< float >( flyZ ) );
	SetColor( shader, "uInk", ink );
	SetColor( shader, "uPaper", paper );

	ScopedSamplerActivation samplerActivation( 0 );
	Scoped2DTextureBinding textureBinding( heightMap );
	shader.Set( "uHeightMap", 0 );

	ScopedVAOBinding vaoBinding( emptyVAO );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	if( entry.family == FAMILY_RADIAL && entry.variant == 4 )
		DrawChords();
	return FF_SUCCESS;
}

void PatternBook::DrawChords()
{
	// Motion 1, Endless: the inner circle keeps turning against the outer one,
	// so the figure rolls round itself; Sway: the factor swings up and back.
	// Motion 2 swings the factor in either mode; Motion 3 makes the inner
	// circle breathe. A hit pushes the factor on and lets it slide back.
	const double tau = 6.283185307179586;
	double swing  = 0.5 - 0.5 * std::cos( tau * phase );
	double k      = ChordFactor( form ) + amount * motions[ 1 ] * 1.5 * swing;
	double offset = 0.0;
	if( loopMode == 1 )
		k += amount * motions[ 0 ] * 1.5 * swing;
	else
		offset = tau * std::fmod( travel[ 0 ], 1.0 );
	float inner = ChordInner( form2 ) * ( 1.0f + 0.35f * amount * motions[ 2 ] * static_cast< float >( std::sin( tau * phase ) ) );
	if( hitCount > 0.5 )
		k += 2.0 * hitAmount * std::exp( -1.5 * hitAge );
	int lines = ChordLines( CellsValue( scale ) );

	ScopedShaderBinding shaderBinding( chordShader.GetGLID() );
	chordShader.Set( "uViewport", static_cast< float >( currentViewport.x ), static_cast< float >( currentViewport.y ),
					 static_cast< float >( currentViewport.width ), static_cast< float >( currentViewport.height ) );
	chordShader.Set( "uCount", static_cast< float >( lines ) );
	chordShader.Set( "uFactor", static_cast< float >( k ) );
	chordShader.Set( "uOffset", static_cast< float >( offset ) );
	chordShader.Set( "uInner", inner );
	// Form 3: below the middle the chords start from only part of the outer
	// circle; above it the inner circle is turned against the outer one.
	chordShader.Set( "uSpan", 0.15f + 0.85f * std::min( form3 * 2.0f, 1.0f ) );
	chordShader.Set( "uTwist", std::max( form3 * 2.0f - 1.0f, 0.0f ) * 3.14159265f );
	chordShader.Set( "uRotation", RotationRadians( rotation ) );
	chordShader.Set( "uWidth", WidthPixels( width ) );
	SetColor( chordShader, "uInk", ink );

	// Premultiplied "over" blending, restored to the host's default afterwards.
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glDrawArrays( GL_TRIANGLES, 0, lines * 6 );
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
	case PID_FORM: form = value; break;
	case PID_FORM2: form2 = value; break;
	case PID_FORM3: form3 = value; break;
	case PID_WIDTH: width = value; break;
	case PID_ROTATION: rotation = value; break;
	case PID_SPEED: speed = value; break;
	case PID_AMOUNT: amount = value; break;
	case PID_MOTION1: motions[ 0 ] = value; break;
	case PID_MOTION2: motions[ 1 ] = value; break;
	case PID_MOTION3: motions[ 2 ] = value; break;
	case PID_LOOP: loopMode = ClampIndex( value, 2 ); break;
	case PID_HIT:
		if( value > 0.5f && !hitPressed )
			hitPending = true;
		hitPressed = value > 0.5f;
		break;
	case PID_HIT_AMOUNT: hitAmount = value; break;
	case PID_HIT_EVERY: hitEvery = ClampIndex( value, kHitCount ); break;
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
	case PID_FORM: return form;
	case PID_FORM2: return form2;
	case PID_FORM3: return form3;
	case PID_WIDTH: return width;
	case PID_ROTATION: return rotation;
	case PID_SPEED: return speed;
	case PID_AMOUNT: return amount;
	case PID_MOTION1: return motions[ 0 ];
	case PID_MOTION2: return motions[ 1 ];
	case PID_MOTION3: return motions[ 2 ];
	case PID_LOOP: return static_cast< float >( loopMode );
	case PID_HIT: return hitPressed ? 1.0f : 0.0f;
	case PID_HIT_AMOUNT: return hitAmount;
	case PID_HIT_EVERY: return static_cast< float >( hitEvery );
	default: return 0.0f;
	}
}

char* PatternBook::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PID_SCALE:
		if( kPatterns[ pattern ].family == FAMILY_RADIAL && kPatterns[ pattern ].variant == 4 )
			return Display( "%.0f lines", static_cast< float >( ChordLines( CellsValue( scale ) ) ) );
		return Display( "%.1f cells", CellsValue( scale ) );
	case PID_FORM:
		if( kPatterns[ pattern ].family == FAMILY_RADIAL && kPatterns[ pattern ].variant == 4 )
			return Display( "factor %.2f", ChordFactor( form ) );
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", kPatterns[ pattern ].form, form * 100.0f );
		return gDisplay;
	case PID_FORM2:
		if( kPatterns[ pattern ].family == FAMILY_RADIAL && kPatterns[ pattern ].variant == 4 )
			return Display( "inner %.2f", ChordInner( form2 ) );
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", kPatterns[ pattern ].form2, form2 * 100.0f );
		return gDisplay;
	case PID_FORM3:
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", kPatterns[ pattern ].form3, form3 * 100.0f );
		return gDisplay;
	case PID_WIDTH: return Display( "%.1f px", WidthPixels( width ) );
	case PID_ROTATION: return Display( "%.0f deg", RotationRadians( rotation ) * 57.2957795f );
	case PID_SPEED:
	{
		float perBar = CyclesPerBar( speed );
		if( perBar < 0.005f )
			return const_cast< char* >( "stopped" );
		if( perBar >= 1.0f )
			return Display( "%.1f per bar", perBar );
		return Display( "1 per %.1f bars", 1.0f / perBar );
	}
	case PID_AMOUNT: return Display( "%.0f%%", amount * 100.0f );
	case PID_MOTION1:
	case PID_MOTION2:
	case PID_MOTION3:
	{
		int m = static_cast< int >( index - PID_MOTION1 );
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", kPatterns[ pattern ].motion[ m ], motions[ m ] * 100.0f );
		return gDisplay;
	}
	case PID_HIT_AMOUNT: return Display( "%.0f%%", hitAmount * 100.0f );
	default: return CFFGLPlugin::GetParameterDisplay( index );
	}
}
