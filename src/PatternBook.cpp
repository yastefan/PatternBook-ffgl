// SPDX-License-Identifier: GPL-3.0-or-later
#include "PatternBook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
	PID_WIDTH,
	PID_ROTATION,

	PID_RATE,
	PID_AMOUNT,
	PID_HIT,
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

// Family and variant in one menu. The shader's family is index / 4, its
// variant index % 4. Once released, append only.
const char* const kPatternNames[] = {
	"Grid: Arcs", "Grid: Diagonals", "Grid: Squares", "Grid: Dots",
	"Radial: Spiral", "Radial: Broken Rings", "Radial: Rays", "Radial: Dot Rings",
	"Iso: Cubes", "Iso: Wire Cubes", "Iso: Hatched Cubes", "Iso: Lattice",
	"Distort: Interference", "Distort: Ripple", "Distort: Wave Lines", "Distort: Moire Rings",
};
const int kPatternCount = 16;
const int kMaxSeed      = 99;

// What Form does in each pattern, for the parameter display.
const char* const kFormMeaning[] = {
	"ribbon", "lines", "size", "dot size",
	"arms", "gaps", "inner radius", "dot size",
	"scatter", "inner cube", "hatching", "weave",
	"wavelength", "wavelength", "frequency", "spacing",
};

// One animation cycle lasts this many bars (4/4); 0 stops the animation.
const double kRateBars[]       = { 0.0, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0, 16.0 };
const char* const kRateNames[] = { "Off", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" };
const int kRateCount           = 8;
// Automatic hits; 0 means only the Hit button fires one.
const double kHitBars[]       = { 0.0, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0 };
const char* const kHitNames[] = { "Off", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
const int kHitCount           = 7;

// The shader's cycle counters wrap here: a multiple of 4 and 6, so quarter
// turns and the wire cubes' sixth turns land where they started.
const double kWrap = 3072.0;

// Mappings from the host's normalised 0..1 values to physical quantities.
float CellsValue( float v ) { return 2.0f + 46.0f * v * v; }// pattern cells across the frame height
float WidthPixels( float v ) { return 0.5f + 23.5f * v * v; }
float RotationRadians( float v ) { return ( v - 0.5f ) * 6.28318530718f; }

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
	rate = 4;// 2 bars per cycle

	SetOptionParamInfo( PID_PATTERN, "Pattern", kPatternCount, static_cast< float >( pattern ) );
	for( int i = 0; i < kPatternCount; ++i )
		SetParamElementInfo( PID_PATTERN, i, kPatternNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PID_SEED, "Seed", FF_TYPE_INTEGER, static_cast< float >( seed ) );
	SetParamRange( PID_SEED, 0.0f, static_cast< float >( kMaxSeed ) );
	SetParamInfo( PID_SCALE, "Scale", FF_TYPE_STANDARD, scale );
	SetParamInfo( PID_FORM, "Form", FF_TYPE_STANDARD, form );
	SetParamInfo( PID_WIDTH, "Line Width", FF_TYPE_STANDARD, width );
	SetParamInfo( PID_ROTATION, "Rotation", FF_TYPE_STANDARD, rotation );
	for( unsigned int id = PID_PATTERN; id <= PID_ROTATION; ++id )
		SetParamGroup( id, "Pattern" );

	SetOptionParamInfo( PID_RATE, "Rate", kRateCount, static_cast< float >( rate ) );
	for( int i = 0; i < kRateCount; ++i )
		SetParamElementInfo( PID_RATE, i, kRateNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PID_AMOUNT, "Amount", FF_TYPE_STANDARD, amount );
	SetParamInfo( PID_HIT, "Hit", FF_TYPE_EVENT, false );
	SetOptionParamInfo( PID_HIT_EVERY, "Hit Every", kHitCount, static_cast< float >( hitEvery ) );
	for( int i = 0; i < kHitCount; ++i )
		SetParamElementInfo( PID_HIT_EVERY, i, kHitNames[ i ], static_cast< float >( i ) );
	for( unsigned int id = PID_RATE; id <= PID_HIT_EVERY; ++id )
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

	if( !shader.Compile( pattern::kFullscreenVS, pattern::kPatternFS ) )
	{
		FFGLLog::LogToHost( "PatternBook: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenVertexArrays( 1, &emptyVAO );
	return CFFGLPlugin::InitGL( vp );
}

FFResult PatternBook::DeInitGL()
{
	shader.FreeGLResources();
	if( emptyVAO )
		glDeleteVertexArrays( 1, &emptyVAO );
	emptyVAO = 0;
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

	// Accumulate rather than divide the bar position, so changing the rate
	// changes the speed without making the pattern jump.
	double bars = std::max( barPosition - before, 0.0 );
	if( kRateBars[ rate ] > 0.0 )
	{
		double cycles = bars / kRateBars[ rate ];
		phase         = std::fmod( phase + cycles, kWrap );
		motion        = std::fmod( motion + cycles * amount, kWrap );
	}

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

FFResult PatternBook::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
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

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "uViewport", static_cast< float >( currentViewport.x ), static_cast< float >( currentViewport.y ),
				static_cast< float >( currentViewport.width ), static_cast< float >( currentViewport.height ) );
	shader.Set( "uFamily", pattern / 4 );
	shader.Set( "uVariant", pattern % 4 );
	shader.Set( "uSeed", static_cast< float >( seed ) );
	shader.Set( "uCells", CellsValue( scale ) );
	shader.Set( "uForm", form );
	shader.Set( "uWidth", WidthPixels( width ) );
	shader.Set( "uRotation", RotationRadians( rotation ) );
	shader.Set( "uPhase", static_cast< float >( phase ) );
	shader.Set( "uMotion", static_cast< float >( motion ) );
	shader.Set( "uAmount", amount );
	shader.Set( "uHitCount", static_cast< float >( hitCount ) );
	shader.Set( "uHitAge", static_cast< float >( hitAge ) );
	SetColor( shader, "uInk", ink );
	SetColor( shader, "uPaper", paper );

	ScopedVAOBinding vaoBinding( emptyVAO );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	return FF_SUCCESS;
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
	case PID_WIDTH: width = value; break;
	case PID_ROTATION: rotation = value; break;
	case PID_RATE: rate = ClampIndex( value, kRateCount ); break;
	case PID_AMOUNT: amount = value; break;
	case PID_HIT:
		if( value > 0.5f && !hitPressed )
			hitPending = true;
		hitPressed = value > 0.5f;
		break;
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
	case PID_WIDTH: return width;
	case PID_ROTATION: return rotation;
	case PID_RATE: return static_cast< float >( rate );
	case PID_AMOUNT: return amount;
	case PID_HIT: return hitPressed ? 1.0f : 0.0f;
	case PID_HIT_EVERY: return static_cast< float >( hitEvery );
	default: return 0.0f;
	}
}

char* PatternBook::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PID_SCALE: return Display( "%.1f cells", CellsValue( scale ) );
	case PID_FORM:
		snprintf( gDisplay, sizeof( gDisplay ), "%s %.0f%%", kFormMeaning[ pattern ], form * 100.0f );
		return gDisplay;
	case PID_WIDTH: return Display( "%.1f px", WidthPixels( width ) );
	case PID_ROTATION: return Display( "%.0f deg", RotationRadians( rotation ) * 57.2957795f );
	case PID_AMOUNT: return Display( "%.0f%%", amount * 100.0f );
	default: return CFFGLPlugin::GetParameterDisplay( index );
	}
}
