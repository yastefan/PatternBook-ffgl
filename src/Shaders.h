// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// GLSL 4.10 core, the highest version macOS offers, so the same code runs in
// Resolume on macOS and Windows.

namespace pattern
{
// Full-screen triangle generated from gl_VertexID; needs no vertex buffers.
static const char kFullscreenVS[] = R"(#version 410 core
void main()
{
	vec2 corner = vec2( ( gl_VertexID << 1 ) & 2, gl_VertexID & 2 );
	gl_Position = vec4( corner * 2.0 - 1.0, 0.0, 1.0 );
}
)";

// The line patterns, computed per pixel.
//
// Every pattern returns the nearest line as a LineInfo: its distance in
// pixels, a position along it (frame units, for dashes), a number that tells
// lines apart (for Thin Out) and a weight that fades lines out where they
// would alias. Shade() then draws it with the shared Lines controls, so
// dashes, thinning and line width work alike everywhere.
//
// Coordinates: the frame's height spans -1..1 ("frame units"), x scales with
// the aspect ratio. Most patterns work in cell units, q = p * cells / 2, so
// Scale sets how many cells fit across the frame height.
//
// Phase 1 and Phase 2 run 0..1 and every pattern uses them so that 0 and 1
// give the same picture: a phase only ever turns something by its symmetry,
// shifts a repeating structure by one repeat, or drives a sine.
//
// The plugin compiles this once per family, prepending "#version 410 core"
// and "#define FAMILY n", so each program only contains one family's code.
// MSVC limits a single string literal to about 16 KB, so the source is split
// into adjacent literals (joined by the compiler, up to 64 KB); keep each
// piece below 16 KB.
static const char kPatternFS[] = R"(
uniform vec4 uViewport;  // x, y, width, height in pixels
uniform int uVariant;    // pattern within the family
uniform float uSeed;
uniform float uCells;    // cells across the frame height
uniform float uForm1, uForm2, uForm3;// pattern-specific shape controls, 0..1
uniform float uWidth;    // line width in pixels
uniform float uRotation; // radians
uniform float uPhase1, uPhase2;// pattern-specific phases, 0..1, looping
uniform float uHitCount, uHitAge, uHitAmount;
uniform float uDash;     // share of each dash period left empty (0 = solid)
uniform float uDashLength;// dash period in frame units
uniform float uDashShift;// 0..1, moves the dashes along their lines
uniform float uThin;     // share of lines left out
uniform int uMirror;     // 0 off, 1 x, 2 y, 3 xy, 4/5/6 kaleidoscope with 6/8/12 segments
uniform float uWarp, uWarpSize;
uniform vec4 uInk;       // premultiplied
uniform vec4 uPaper;     // premultiplied
uniform vec3 uCamera;    // Field: Terrain camera position (x, height, z)
uniform sampler2D uHeightMap;// Field: Terrain landscape
out vec4 fragColor;

const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;
const float HIT_SPEED = 1.5; // frame units per second

float gPixel;     // one pixel in frame units
float gHalfWidth; // half the line width in pixels
vec2 gScreen;     // this pixel in frame units, before any transform

// ---------------------------------------------------------------- helpers

float Hash( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.x + p3.y ) * p3.z );
}

vec2 Hash22( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * vec3( 0.1031, 0.1030, 0.0973 ) );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.xx + p3.yz ) * p3.zy ) * 2.0 - 1.0;
}

vec2 Rot( vec2 v, float a )
{
	float c = cos( a ), s = sin( a );
	return vec2( c * v.x - s * v.y, s * v.x + c * v.y );
}

float Segment( vec2 p, vec2 a, vec2 b )
{
	vec2 pa = p - a, ba = b - a;
	float h = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-12 ), 0.0, 1.0 );
	return length( pa - ba * h );
}

// Gradient noise, roughly -0.7..0.7.
float Noise( vec2 p )
{
	vec2 i = floor( p ), f = fract( p );
	vec2 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float a = dot( Hash22( i ), f );
	float b = dot( Hash22( i + vec2( 1.0, 0.0 ) ), f - vec2( 1.0, 0.0 ) );
	float c = dot( Hash22( i + vec2( 0.0, 1.0 ) ), f - vec2( 0.0, 1.0 ) );
	float d = dot( Hash22( i + 1.0 ), f - 1.0 );
	return mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

// Fractal noise; gain sets how strong the finer octaves are (0.5 is classic).
float Fbm( vec2 p, int octaves, float gain )
{
	float sum = 0.0, amp = 0.5;
	for( int i = 0; i < octaves; ++i )
	{
		sum += amp * Noise( p );
		p = mat2( 1.6, 1.2, -1.2, 1.6 ) * p;
		amp *= gain;
	}
	return sum * 2.0;
}

// Fractal noise that changes as the phase runs and returns to where it
// started: two independent fields blended by cosine and sine.
float LoopFbm( vec2 p, float gain, float phase )
{
	return Fbm( p + vec2( 17.3, 9.1 ), 3, gain ) * cos( TAU * phase ) + Fbm( p + vec2( -31.7, 44.2 ), 3, gain ) * sin( TAU * phase );
}

// Steps from hits for the cell at distance dist (frame units) from the
// centre: each hit turns a Hit Amount share of the cells by one step as its
// wave passes.
float HitTurns( float dist, vec2 cell )
{
	if( uHitCount < 0.5 || Hash( cell + 29.3 ) >= uHitAmount )
		return 0.0;
	float progress = clamp( ( uHitAge * HIT_SPEED - dist ) / 0.35, 0.0, 1.0 );
	return uHitCount - 1.0 + smoothstep( 0.0, 1.0, progress );
}

// A ring of energy travelling out from the centre after a hit, fading out;
// 1 at the default Hit Amount of 50 %.
float HitPulse( float dist )
{
	if( uHitCount < 0.5 )
		return 0.0;
	float x = ( dist - uHitAge * HIT_SPEED ) / 0.18;
	return exp( -x * x ) * exp( -uHitAge * 0.9 ) * 2.0 * uHitAmount;
}

// Steps of one cell, once per cycle of Phase 1: a wave runs out from the
// centre, a little ragged, and each cell takes perCycle steps as it passes.
// Choose perCycle so that many steps bring the cell back to how it looked.
float CellSteps( vec2 cell, float dist, float perCycle )
{
	float s = uPhase1 + 0.6 * Hash( cell + 71.3 ) + 0.3 * dist;
	return perCycle * ( floor( s ) + smoothstep( 0.82, 1.0, fract( s ) ) );
}

// ---------------------------------------------------------------- lines

struct LineInfo
{
	float d;     // distance in pixels
	float along; // position along the line, frame units
	float id;    // tells lines apart
	float weight;// 0..1
};

LineInfo NoLine() { return LineInfo( 1e9, 0.0, 0.0, 0.0 ); }

LineInfo Nearer( LineInfo a, LineInfo b ) { return b.d < a.d ? b : a; }

// A line at distance dist, where px is one pixel in the same unit.
LineInfo DistLine( float dist, float px, float along, float id ) { return LineInfo( dist / px, along, id, 1.0 ); }

// Lines wherever the smooth field g is a whole number. The screen-space
// derivative turns the field distance into pixels, so this works for any
// warp; it also gives the direction along the line for dashes. Lines closer
// than about two pixels fade out instead of aliasing.
LineInfo LevelLine( float g )
{
	vec2 grad      = vec2( dFdx( g ), dFdy( g ) );
	float perPixel = max( length( grad ), 1e-6 );
	vec2 tangent   = vec2( -grad.y, grad.x ) / perPixel;
	float id       = round( g );
	return LineInfo( abs( g - id ) / perPixel, dot( gScreen, tangent ), id, clamp( 2.0 - 2.0 * perPixel, 0.0, 1.0 ) );
}

// Coverage of the nearest line, with the shared Lines controls applied.
float Shade( LineInfo l )
{
	if( l.weight <= 0.0 )
		return 0.0;
	if( uThin > 0.0 && Hash( vec2( l.id, 91.7 ) ) < uThin )
		return 0.0;
	float d = l.d;
	if( uDash > 0.0 )
	{
		float period = uDashLength;
		float duty   = 1.0 - 0.9 * uDash;
		float w      = fract( l.along / period + uDashShift ) * period;// position within one period
		float gap    = w > duty * period ? min( w - duty * period, period - w ) : 0.0;
		d            = length( vec2( d, gap / gPixel ) );
	}
	return clamp( gHalfWidth - d + 0.5, 0.0, 1.0 ) * l.weight;
}

// Distance from s to the nearest of m evenly spaced values in -band..band
// (just 0 for a single line), and which one it is.
float NearestOf( float s, float band, float m, out float index )
{
	index = 0.0;
	if( m < 1.5 || band < 1e-4 )
		return abs( s );
	float step = 2.0 * band / ( m - 1.0 );
	index      = clamp( round( ( s + band ) / step ), 0.0, m - 1.0 );
	return abs( s - ( -band + index * step ) );
}
)"
R"(
// ---------------------------------------------------------------- tiles

// Does a cell stay empty, or get the crossing tile on top? Above the middle
// the density control leaves cells empty, below it it doubles them.
bool TileEmpty( vec2 cell, float density ) { return Hash( cell + 41.0 ) < ( density - 0.5 ) * 1.6; }
bool TileDouble( vec2 cell, float density ) { return Hash( cell + 41.0 ) < ( 0.5 - density ) * 2.0; }

// Quarter-circle arcs round two opposite corners of each cell (Truchet).
// Form 1 widens each arc into a ribbon, Form 2 sets how many lines run along
// it, Form 3 the density. Phase 1 turns the tiles by half turns, which look
// the same; Phase 2 makes the ribbons breathe.
LineInfo TilesArcs( vec2 q, float px )
{
	vec2 cell  = floor( q );
	vec2 f     = q - cell - 0.5;
	float dist = length( cell + 0.5 ) * 2.0 / uCells;
	if( TileEmpty( cell, uForm3 ) )
		return NoLine();
	bool both     = TileDouble( cell, uForm3 );
	float quarter = step( 0.5, Hash( cell ) ) + CellSteps( cell, dist, 2.0 ) + HitTurns( dist, cell );
	f             = Rot( f, quarter * 0.5 * PI );
	float band    = max( uForm1 * 0.2 + 0.07 * sin( TAU * uPhase2 ) * cos( dist * 4.0 ), 0.0 );
	float m       = 1.0 + floor( uForm2 * 3.99 );
	LineInfo best = NoLine();
	for( int c = 0; c < 4; ++c )
	{
		if( c >= 2 && !both )
			break;
		vec2 corner = c == 0 ? vec2( 0.5 ) : ( c == 1 ? vec2( -0.5 ) : ( c == 2 ? vec2( 0.5, -0.5 ) : vec2( -0.5, 0.5 ) ) );
		vec2 v      = f - corner;
		float index;
		float d     = NearestOf( length( v ) - 0.5, band, m, index );
		float id    = dot( cell, vec2( 17.0, 131.0 ) ) * 8.0 + float( c ) * 4.0 + index;
		best        = Nearer( best, DistLine( d, px, atan( v.y, v.x ) / uCells, id ) );
	}
	return best;
}

// Diagonals across each cell (10 PRINT). Form 1 adds parallel lines (their
// spacing divides the cell edge, so neighbouring tiles always meet), Form 2
// sets the density, Form 3 biases the direction. Phase 1 turns tiles by half
// turns; Phase 2 makes the lines pulse wider.
LineInfo TilesDiagonals( vec2 q, float px )
{
	vec2 cell  = floor( q );
	vec2 f     = q - cell - 0.5;
	float dist = length( cell + 0.5 ) * 2.0 / uCells;
	if( TileEmpty( cell, uForm2 ) )
		return NoLine();
	bool both     = TileDouble( cell, uForm2 );
	float quarter = step( uForm3, Hash( cell ) ) + CellSteps( cell, dist, 2.0 ) + HitTurns( dist, cell );
	f             = Rot( f, quarter * 0.5 * PI );
	float k       = 1.0 + floor( uForm1 * 4.99 );
	float pulse   = 1.0 + 1.2 * sin( PI * uPhase2 ) * sin( PI * uPhase2 ) * ( 0.75 + 0.25 * cos( dist * 6.0 ) );
	LineInfo best = NoLine();
	for( int c = 0; c < 2; ++c )
	{
		if( c == 1 && !both )
			break;
		vec2 g   = c == 0 ? f : vec2( f.x, -f.y );
		float t  = ( g.x - g.y ) * k;
		float i  = round( t );
		float d  = abs( t - i ) / k * 0.70710678 / pulse;
		float id = dot( cell, vec2( 17.0, 131.0 ) ) * 16.0 + float( c ) * 8.0 + i;
		best     = Nearer( best, DistLine( d, px, ( g.x + g.y ) * 0.70710678 * 2.0 / uCells, id ) );
	}
	return best;
}

// Pointy-top hexagon cells with circumradius 1. Returns the offset from the
// cell centre (xy) and the centre itself (zw).
vec4 HexCell( vec2 p )
{
	const vec2 r = vec2( 1.7320508, 3.0 );
	vec2 h       = r * 0.5;
	vec2 a       = mod( p, r ) - h;
	vec2 b       = mod( p - h, r ) - h;
	vec2 g       = dot( a, a ) < dot( b, b ) ? a : b;
	return vec4( g, p - g );
}

// Truchet on hexagons: three arcs round every other corner join the middles
// of neighbouring edges, so all tiles connect. Controls as for Arcs; Phase 1
// turns tiles by thirds of a turn, which look the same.
LineInfo TilesHexArcs( vec2 q, float pxCell )
{
	vec2 P     = q * 1.7320508;// hexagons one cell wide
	vec4 hc    = HexCell( P );
	vec2 key   = round( hc.zw * vec2( 1.1547005, 0.6666667 ) );
	float dist = length( hc.zw ) / 1.7320508 * 2.0 / uCells;
	float px   = pxCell * 1.7320508;
	if( TileEmpty( key, uForm3 ) )
		return NoLine();
	bool both     = TileDouble( key, uForm3 );
	float steps   = step( 0.5, Hash( key ) ) + CellSteps( key, dist, 2.0 ) + HitTurns( dist, key );// sixths of a turn
	vec2 g        = Rot( hc.xy, steps * PI / 3.0 );
	float band    = max( uForm1 * 0.15 + 0.05 * sin( TAU * uPhase2 ) * cos( dist * 4.0 ), 0.0 );
	float m       = 1.0 + floor( uForm2 * 3.99 );
	LineInfo best = NoLine();
	for( int c = 0; c < 6; ++c )
	{
		if( ( c & 1 ) == 1 && !both )
			continue;
		float a = PI * 0.5 + float( c ) * PI / 3.0;
		vec2 v  = g - vec2( cos( a ), sin( a ) );
		float index;
		float d  = NearestOf( length( v ) - 0.5, band, m, index );
		float id = dot( key, vec2( 17.0, 131.0 ) ) * 32.0 + float( c ) * 4.0 + index;
		best     = Nearer( best, DistLine( d, px, atan( v.y, v.x ) / uCells / 1.7320508, id ) );
	}
	return best;
}

// Square outlines in each cell. Form 1 sets their size, Form 2 how many are
// nested, Form 3 rounds them into circles. Phase 1 turns them a quarter
// turn, as a diagonal wave; Phase 2 makes them breathe.
LineInfo TilesSquares( vec2 q, float px )
{
	vec2 cell  = floor( q );
	vec2 f     = q - cell - 0.5;
	float dist = length( cell + 0.5 ) * 2.0 / uCells;
	float turn = uPhase1 - 0.12 * ( cell.x + cell.y ) + HitTurns( dist, cell );
	vec2 g     = abs( Rot( f, turn * 0.5 * PI ) );
	// At most 0.35, so a turned square stays inside its cell.
	float size = clamp( mix( 0.1, 0.35, uForm1 ) * ( 1.0 + 0.4 * sin( TAU * uPhase2 ) * cos( dist * 4.0 ) ), 0.03, 0.35 );
	float nest = 1.0 + floor( uForm2 * 3.99 );
	LineInfo best = NoLine();
	for( int i = 0; i < 4; ++i )
	{
		if( float( i ) >= nest )
			break;
		float sz = size * ( 1.0 - float( i ) / nest );
		float rr = uForm3 * sz;
		vec2 e   = g - ( sz - rr );
		float sd = length( max( e, 0.0 ) ) + min( max( e.x, e.y ), 0.0 ) - rr;
		float id = dot( cell, vec2( 17.0, 131.0 ) ) * 4.0 + float( i );
		best     = Nearer( best, DistLine( abs( sd ), px, atan( f.y, f.x ) * sz * 2.0 / uCells, id ) );
	}
	return best;
}
)"
R"(
// ---------------------------------------------------------------- radial

// Spiral with Form 1 arms; Form 2 bends the even spacing (tighter outside
// below the middle, tighter inside above it), Form 3 opens a hole. Phase 1
// lets the arms flow outwards by one turn, which looks the same; Phase 2
// makes the spacing breathe.
LineInfo RadialSpiral( vec2 p )
{
	float r     = max( length( p ), 1e-4 );
	float a     = atan( p.y, p.x );
	float n     = 1.0 + floor( uForm1 * 7.99 );
	float s     = 2.0 / uCells * ( 1.0 + 0.25 * sin( TAU * uPhase2 ) );
	float power = mix( 0.6, 1.6, uForm2 );
	float rp    = pow( r, power );
	float g     = rp / s - n * a / TAU - uPhase1 + HitPulse( r );
	// Analytic gradient: dFdx would see the jump where atan wraps.
	float grad  = length( vec2( power * rp / r / s, n / ( TAU * r ) ) );
	float i     = round( g );
	float d     = max( abs( g - i ) / grad, uForm3 * 0.8 - r );
	return DistLine( d, gPixel, rp * rp / s * 2.0, mod( i, n ) );
}

// Concentric rings broken into arcs. Form 1 sets the gaps, Form 2 up to how
// many arcs a ring has, Form 3 how differently the rings spin. Phase 1 turns
// every ring by whole arcs, Phase 2 makes the gaps breathe.
LineInfo RadialBrokenRings( vec2 p )
{
	float s      = 2.0 / uCells;
	float r      = length( p );
	float a      = atan( p.y, p.x );
	float j      = max( round( r / s ), 1.0 );
	float dir    = Hash( vec2( j, 9.0 ) ) < 0.5 ? -1.0 : 1.0;
	float speed  = dir * ( 1.0 + floor( Hash( vec2( j, 3.0 ) ) * uForm3 * 3.99 ) );// whole arcs per cycle
	float segs   = 1.0 + floor( Hash( vec2( j, 5.0 ) ) * mix( 1.0, 10.0, uForm2 ) );
	float x      = ( a + TAU * speed * uPhase1 / segs ) / TAU * segs;
	float u      = fract( x );
	float gap    = clamp( uForm1 * 0.6 + HitPulse( r ) * 0.4 + 0.3 * sin( TAU * uPhase2 ) * sin( j * 1.7 ), 0.0, 0.95 );
	// Distance past the end of the arc, measured along the ring.
	float past   = max( abs( u - 0.5 ) - 0.5 * ( 1.0 - gap ), 0.0 ) * TAU * r / segs;
	float across = abs( r - j * s );
	return DistLine( length( vec2( across, past ) ), gPixel, a * r, j * 16.0 + mod( floor( x ), segs ) );
}

// Rays between an inner radius (Form 1) and lengths that vary (Form 3);
// Form 2 sets how many. Phase 1 turns them once round, Phase 2 makes their
// lengths pulse in and out.
LineInfo RadialRays( vec2 p )
{
	float r       = length( p );
	float a       = atan( p.y, p.x );
	float n       = max( round( uCells * 3.0 * mix( 0.25, 1.75, uForm2 ) ), 3.0 );
	float u       = a / TAU * n + uPhase1 * n;
	float i       = round( u );
	float da      = ( u - i ) / n * TAU;// angle to the nearest ray
	float k       = mod( i, n );
	float h       = Hash( vec2( k, 1.0 ) );
	float inner   = mix( 0.05, 0.7, uForm1 );
	float longest = 1.7 - uForm3 * Hash( vec2( k, 7.0 ) ) * ( 1.7 - inner - 0.1 );
	longest       = inner + ( longest - inner ) * ( 1.0 + 0.3 * sin( TAU * uPhase2 ) * cos( TAU * h ) ) + 0.4 * HitPulse( r );
	float onRay   = r * cos( da );
	float beside  = abs( r * sin( da ) );
	float past    = max( max( inner - onRay, onRay - longest ), 0.0 );
	return DistLine( length( vec2( beside, past ) ), gPixel, onRay, k );
}

// Nested polygons, a tunnel. Form 1 sets the sides (3 to 10; PolygonSides in
// PatternBook.cpp), Form 2 twists the rings against each other (none at the
// middle), Form 3 pinches them into stars below the middle and rounds them
// towards circles above it. Phase 1 lets the rings flow outwards by one, so
// the tunnel runs endlessly; Phase 2 turns it by one side.
LineInfo RadialPolygons( vec2 p )
{
	float n      = 3.0 + floor( uForm1 * 7.99 );
	float sector = TAU / n;
	float s      = 2.0 / uCells;
	float r      = length( p );
	// Each ring is turned by an amount that depends on its radius only, so
	// rings keep their twist as they flow and a full cycle looks the same.
	float a      = atan( p.y, p.x ) - sector * uPhase2 - ( uForm2 - 0.5 ) * 0.5 * sector * r / s;
	float t      = mod( a, sector ) - 0.5 * sector;// angle from the middle of a side
	float shape  = cos( 0.5 * sector ) / cos( t );   // the polygon's radius at this angle, 1 at a corner
	float star   = clamp( ( 0.5 - uForm3 ) * 2.0, 0.0, 1.0 );
	float round_ = clamp( ( uForm3 - 0.5 ) * 2.0, 0.0, 1.0 );
	shape        = mix( shape, 0.45 + 0.55 * abs( t ) / ( 0.5 * sector ), star );
	shape        = mix( shape, 1.0, round_ );
	LineInfo l   = LevelLine( r / shape / s - uPhase1 + HitPulse( r ) * 0.6 );
	l.along      = a * r;
	return l;
}

// Guilloche rosette: N closed curves r = R + A cos(k angle + phase_i), each a
// little turned, crossing into the woven bands of banknotes and watch dials.
// Form 1 sets the lobes (3 to 24; RosetteLobes in PatternBook.cpp), Form 2
// their depth, Form 3 above the middle splits it into two or three nested
// bands. Scale sets how many curves. Phase 1 turns them by one curve, Phase 2 makes the lobes breathe.
LineInfo RadialRosette( vec2 p )
{
	float k       = 3.0 + floor( uForm1 * 21.99 );
	float curves  = clamp( round( uCells ), 4.0, 48.0 );
	float bands   = min( 1.0 + floor( max( uForm3 - 0.5, 0.0 ) * 6.0 ), 3.0 );// one band up to the middle, then two and three
	float spacing = 0.75 / bands;
	float r       = length( p );
	float theta   = atan( p.y, p.x );
	float b       = clamp( floor( ( r - 0.15 ) / spacing ), 0.0, bands - 1.0 );
	float R       = 0.15 + ( b + 0.5 ) * spacing;
	float A       = mix( 0.1, 0.48, uForm2 ) * spacing * ( 1.0 + 0.3 * sin( TAU * uPhase2 ) ) * ( 1.0 + HitPulse( r ) );
	// The curve through this pixel has phase +-acos((r - R) / A) - k theta.
	// Check the curves on either side of that phase on both branches.
	float base    = acos( clamp( ( r - R ) / A, -1.0, 1.0 ) );
	float best    = 1e9, bestId = 0.0;
	for( int branch = 0; branch < 2; ++branch )
	{
		float phi = ( branch == 0 ? base : -base ) - k * theta;
		float x   = phi / TAU * curves - uPhase1;
		for( int side = 0; side < 2; ++side )
		{
			float i    = floor( x ) + float( side );
			float w    = k * theta + TAU * ( i + uPhase1 ) / curves;
			float f    = r - R - A * cos( w );
			float grad = length( vec2( 1.0, A * k * sin( w ) / max( r, 1e-4 ) ) );
			float d    = abs( f ) / grad;
			if( d < best )
			{
				best   = d;
				bestId = mod( i, curves ) + b * 128.0;
			}
		}
	}
	return DistLine( best, gPixel, theta * r, bestId );
}
)"
R"(
// ---------------------------------------------------------------- isometric

// Signed distance to the pointy-top hexagon with circumradius 1.
float HexDistance( vec2 g )
{
	g = abs( g );
	return max( g.x, dot( g, vec2( 0.5, 0.8660254 ) ) ) - 0.8660254;
}

// Distance to the three edges from the cube's centre corner, turned by a.
float CubeEdges( vec2 g, float a )
{
	float d = 1e9;
	for( int i = 0; i < 3; ++i )
	{
		float e = a + ( -0.5 + float( i ) * 2.0 / 3.0 ) * PI;// -90, 30 and 150 degrees
		d       = min( d, Segment( g, vec2( 0.0 ), vec2( cos( e ), sin( e ) ) ) );
	}
	return d;
}

// Which face of the cube g lies on: 0 right, 1 top, 2 left.
float CubeFace( vec2 g )
{
	return floor( mod( atan( g.y, g.x ) + 0.5 * PI, TAU ) / ( TAU / 3.0 ) );
}

// Wireframe cubes on a hexagonal grid. Form 1 adds a smaller cube inside,
// Form 2 fades the outline in, Form 3 sets their size. Phase 1 flips each
// cube inside out and back (a third of a turn of its inner corner); Phase 2
// makes the inner cubes breathe.
LineInfo IsoWireCubes( vec2 q, float pxCell )
{
	vec2 P     = q * 1.7320508;// hexagons one cell wide
	vec4 hc    = HexCell( P );
	vec2 key   = round( hc.zw * vec2( 1.1547005, 0.6666667 ) );
	float dist = length( hc.zw ) / 1.7320508 * 2.0 / uCells;
	float px   = pxCell * 1.7320508;
	float a    = ( CellSteps( key, dist, 2.0 ) + HitTurns( dist, key ) ) * PI / 3.0;
	float size = mix( 0.5, 1.0, uForm3 );
	vec2 gc    = hc.xy / size;
	float d    = CubeEdges( gc, a ) * size;
	if( uForm1 > 0.02 )
	{
		float inner = mix( 0.15, 0.85, uForm1 ) * ( 1.0 + 0.25 * sin( TAU * uPhase2 ) * cos( dist * 3.0 ) );
		vec2 gi     = gc / inner;
		d           = min( d, min( abs( HexDistance( gi ) ), CubeEdges( gi, -a ) ) * inner * size );
	}
	float id      = dot( key, vec2( 17.0, 131.0 ) );
	float along   = atan( gc.y, gc.x ) * size * 2.0 / uCells / 1.7320508;
	LineInfo l    = DistLine( d, px, along, id );
	float outline = clamp( uForm2 * 2.0, 0.0, 1.0 );
	if( outline > 0.0 )
	{
		LineInfo o = DistLine( abs( HexDistance( gc ) ) * size, px, along, id + 0.5 );
		o.weight   = outline;
		l          = Nearer( l, o );
	}
	return l;
}

// Cubes shaded by hatching only: vertical lines on the side faces (twice as
// many on the darker right face), and on the top face from Form 2 on. Form 1
// sets the hatching, Form 3 the cube size. Phase 1 slides the hatching by one
// line, Phase 2 makes the cubes shrink and grow in a wave.
LineInfo IsoHatchedCubes( vec2 q, float pxCell )
{
	vec2 P        = q * 1.7320508;
	vec4 hc       = HexCell( P );
	vec2 key      = round( hc.zw * vec2( 1.1547005, 0.6666667 ) );
	float dist    = length( hc.zw ) / 1.7320508 * 2.0 / uCells;
	float px      = pxCell * 1.7320508;
	float scale   = mix( 0.5, 1.2, uForm3 ) * ( 1.0 - 0.3 * sin( TAU * uPhase2 ) * ( 0.5 + 0.5 * cos( dist * 5.0 ) ) );
	scale         = max( scale * ( 1.0 - 0.5 * HitPulse( dist ) ), 0.05 );
	vec2 gs       = hc.xy / scale;
	float sd      = HexDistance( gs ) * scale;
	float face    = CubeFace( gs );
	float edges   = min( CubeEdges( gs, 0.0 ) * scale, abs( sd ) );
	float density = ( 2.0 + floor( uForm1 * 6.0 ) ) * ( face < 0.5 ? 2.0 : 1.0 );
	float t       = gs.x * density + uPhase1;
	if( face > 0.5 && face < 1.5 )
	{
		density = floor( uForm2 * 8.0 ) - 1.0;
		t       = dot( gs, vec2( 0.8660254, 0.5 ) ) * max( density, 1.0 ) + uPhase1;
	}
	float hatch = density < 0.5 ? 1e9 : max( abs( t - round( t ) ) / max( density, 1.0 ) * scale, sd );// only inside the cube
	float id    = dot( key, vec2( 17.0, 131.0 ) ) * 64.0;
	return Nearer( DistLine( edges, px, gs.x * scale * 2.0 / uCells, id ), DistLine( hatch, px, gs.y * scale * 2.0 / uCells, id + 1.0 + round( t ) ) );
}

// Three families of parallel lines on the isometric axes. Form 1 weaves them,
// Form 2 sets the weave length, Form 3 fades in one, two, then three
// families. Phase 1 glides them by one line, the middle family against the
// others; Phase 2 lets the weave travel.
LineInfo IsoLattice( vec2 q )
{
	LineInfo best = NoLine();
	for( int i = 0; i < 3; ++i )
	{
		float fi  = float( i );
		float ang = PI * 0.5 + fi * TAU / 3.0;
		vec2 n    = vec2( cos( ang ), sin( ang ) );
		vec2 t    = vec2( -n.y, n.x );
		float g   = dot( q, n ) * 1.1547005 + uPhase1 * ( i == 1 ? -1.0 : 1.0 );
		g += uForm1 * 0.35 * sin( dot( q, t ) * mix( 0.3, 2.3, uForm2 ) + TAU * uPhase2 + fi );
		g += HitPulse( length( q ) * 2.0 / uCells ) * 0.5;
		LineInfo l = LevelLine( g );
		l.id      += fi * 4096.0;
		l.weight  *= i == 0 ? 1.0 : ( i == 1 ? smoothstep( 0.1, 0.3, uForm3 ) : smoothstep( 0.35, 0.55, uForm3 ) );
		if( l.weight > 0.0 )
			best = Nearer( best, l );
	}
	return best;
}

// ---------------------------------------------------------------- field

// Streams that run across the frame and bend round eddies; level lines of a
// stream function, so they never cross. Form 1 sets the turbulence, Form 2
// the size of the eddies, Form 3 the fine detail. Phase 1 lets the eddies
// change and come back, Phase 2 swirls the middle.
LineInfo FieldFlowLines( vec2 q )
{
	float r   = length( q ) * 2.0 / uCells;
	vec2 s    = Rot( q, HitPulse( r ) * 1.5 + 1.2 * sin( TAU * uPhase2 ) * exp( -r * 1.5 ) );
	float amp = mix( 0.6, 6.0, uForm1 );
	return LevelLine( s.y + amp * LoopFbm( s * mix( 0.03, 0.2, uForm2 ), mix( 0.15, 0.75, uForm3 ), uPhase1 ) );
}

// Contour lines of a noise landscape, every fifth one heavier. Form 1 sets
// how many levels, Form 2 the size of the hills, Form 3 the fine detail.
// Phase 1 lets the contours rise by five levels, so the heavier ones land
// where the next heavier ones were; Phase 2 lets the landscape change and
// come back.
LineInfo FieldContours( vec2 q )
{
	float levels = mix( 3.0, 16.0, uForm1 );
	float r      = length( q ) * 2.0 / uCells;
	float h      = LoopFbm( q * mix( 0.025, 0.16, uForm2 ), mix( 0.15, 0.75, uForm3 ), uPhase2 ) + 0.5 * HitPulse( r );
	LineInfo l   = LevelLine( h * levels * 2.5 + 5.0 * uPhase1 );
	if( mod( l.id, 5.0 ) == 0.0 )
		l.d *= 0.5;// twice as wide
	return l;
}
)"
R"(
// The landscape comes from a tileable height map the plugin builds once:
// a texture lookup per ray step is far cheaper than noise. Seed shifts it.
// Along the line the camera flies, the mountains sink into a valley, so the
// camera flies through them rather than over them; Form 2 sets the valley's
// width. The plugin samples the same map to keep the camera just above the
// valley floor (TerrainFloor in PatternBook.cpp must match).
float TerrainHeight( vec2 xz, float amp )
{
	float h       = amp * textureLod( uHeightMap, xz * 0.55 / 16.0 + uSeed * vec2( 0.37, 0.71 ), 0.0 ).r;
	vec2 across   = vec2( cos( uRotation ), -sin( uRotation ) );// sideways to the flight
	float lateral = dot( xz - uCamera.xz, across ) / ( ( 1.4 + 0.3 * amp ) * mix( 0.3, 2.5, uForm2 ) );
	return h * ( 1.0 - 0.85 * exp( -lateral * lateral ) );
}

// A wireframe landscape that the camera flies through. Form 1 raises the
// mountains, Form 2 widens the valley, Form 3 sets how far it can see.
// Rotation turns the camera and the flight. Phase 1 moves the camera on by
// one tile of the landscape (seamless when flying straight along an axis);
// Phase 2 makes it roll and look around.
LineInfo FieldTerrain( vec2 p )
{
	float amp = 0.1 + 4.0 * uForm1 * uForm1;// TerrainAmplitude in PatternBook.cpp
	if( uHitCount > 0.5 )
		amp *= 1.0 + 2.4 * uHitAmount * exp( -uHitAge * 2.0 );// a hit makes the mountains jump
	vec3 ro  = uCamera;
	p        = Rot( p, 0.15 * sin( TAU * uPhase2 ) );
	vec3 rd  = normalize( vec3( p.x, p.y - 0.05, 1.4 ) );
	rd.xz    = Rot( rd.xz, -uRotation - 0.35 * sin( TAU * uPhase2 ) );

	float t = 0.05, prev = t;
	bool hit = false;
	for( int i = 0; i < 96; ++i )
	{
		vec3 pos = ro + rd * t;
		float d  = pos.y - TerrainHeight( pos.xz, amp );
		if( d < 0.0 )
		{
			hit = true;
			break;
		}
		// Rising above the highest possible peak: this pixel sees the sky.
		if( rd.y >= 0.0 && pos.y > amp )
			break;
		prev = t;
		// Smaller steps for steeper mountains, so rays don't jump through ridges.
		t += max( 0.45 * d / ( 1.0 + 0.35 * amp ), 0.02 + 0.015 * t );
		if( t > 30.0 )
			break;
	}
	float a = prev, b = t;
	for( int i = 0; i < 5; ++i )
	{
		float mid = 0.5 * ( a + b );
		vec3 pos  = ro + rd * mid;
		if( pos.y < TerrainHeight( pos.xz, amp ) )
			b = mid;
		else
			a = mid;
	}
	// Grid lines on the ground, computed for every pixel so the screen-space
	// derivatives stay valid; pixels that see the sky get none.
	vec3 pos   = ro + rd * b;
	float dens = uCells / 3.5;
	LineInfo z = LevelLine( pos.z * dens );
	z.id      += 4096.0;
	LineInfo l = Nearer( LevelLine( pos.x * dens ), z );
	l.weight  *= hit ? exp( -b * mix( 0.35, 0.04, uForm3 ) ) : 0.0;
	return l;
}

// Scattered, overlapping circles of concentric rings. Form 1 sets the rings
// per circle, Form 2 how many sites hold a circle, Form 3 how different their
// sizes are. Phase 1 lets the rings flow outwards by one, Phase 2 sends the
// circles round small loops.
LineInfo FieldCircles( vec2 q, float pxCell )
{
	const float S = 2.0;// cells per circle site
	vec2 cell     = floor( q / S );
	float rings   = 1.0 + floor( uForm1 * 3.99 );
	LineInfo best = NoLine();
	// Radius and drift are capped so that only the 3 x 3 nearest sites can reach this pixel.
	for( int y = -1; y <= 1; ++y )
		for( int x = -1; x <= 1; ++x )
		{
			vec2 id = cell + vec2( x, y );
			if( Hash( id + 5.1 ) > mix( 0.25, 1.0, uForm2 ) )
				continue;
			float h  = Hash( id + 1.3 ), h2 = Hash( id + 2.7 );
			vec2 jit = 0.25 * ( vec2( sin( TAU * ( uPhase2 + h ) ), cos( TAU * ( uPhase2 + h2 ) ) ) - vec2( sin( TAU * h ), cos( TAU * h2 ) ) );
			vec2 c   = ( id + 0.5 + jit ) * S;
			float R  = S * ( 0.6 + uForm3 * 1.1 * ( Hash( id + 3.9 ) - 0.5 ) );
			R        = min( R * ( 1.0 + 0.5 * HitPulse( length( c ) * 2.0 / uCells ) ), 0.95 * S );
			vec2 v   = q - c;
			float rr = length( v );
			// The rings flow outwards, appearing in the middle and fading at the edge.
			float sp   = R / rings;
			float j    = clamp( round( rr / sp - uPhase1 ), 0.0, rings );
			float rj   = ( j + uPhase1 ) * sp;
			float fade = smoothstep( 0.0, 0.3, rj / sp ) * ( 1.0 - smoothstep( rings, rings + 0.6, j + uPhase1 ) );
			LineInfo l = DistLine( abs( rr - rj ), pxCell, atan( v.y, v.x ) * rr * 2.0 / uCells, dot( id, vec2( 17.0, 131.0 ) ) * 8.0 + j );
			l.weight   = fade;
			if( fade > 0.0 )
				best = Nearer( best, l );
		}
	return best;
}
)"
R"(
// ---------------------------------------------------------------- waves

// Lines rippling like fabric. Form 1 sets the frequency, Form 2 adds a
// second, crossing wave, Form 3 how much the waves change from line to line.
// Phase 1 lets the waves travel by one wavelength, Phase 2 drifts the lines
// by one.
LineInfo WavesWaves( vec2 q )
{
	float k = mix( 0.08, 1.6, uForm1 * uForm1 );
	float t = TAU * uPhase1;
	float g = q.y + uPhase2;
	g += 0.9 * sin( q.x * k + t ) * cos( q.y * k * 1.5 * uForm3 - t );
	g += 0.8 * uForm2 * sin( q.x * k * 2.3 - q.y * k * 0.7 - t );
	g += HitPulse( length( q ) * 2.0 / uCells ) * 0.8;
	return LevelLine( g );
}

// Many horizontal lines, each bent by the same three sine generators but with
// a phase that shifts from line to line, so neighbouring lines fan out and
// cross where the generators resonate. Lines are drawn one by one (not as
// level lines of one field), which is what lets them cross. Form 1 sets the
// height of the band, Form 2 how strongly the lines cross, Form 3 how calm
// the left side stays. Phase 1 drifts the generators, Phase 2 moves the band.
LineInfo WavesResonance( vec2 p )
{
	float count   = clamp( round( uCells * 3.0 ), 8.0, 128.0 );
	float spacing = 2.0 / count;
	float band    = mix( 0.1, 0.9, uForm1 );
	float centre  = ( Hash( vec2( 8.0, 1.0 ) ) - 0.5 ) * 0.4 + 0.5 * sin( TAU * uPhase2 );
	float amp     = 0.45 + 0.5 * HitPulse( abs( p.x ) );// a hit runs out sideways from the middle
	float aspect  = uViewport.z / uViewport.w;
	float calm    = min( uForm3 * 1.6, 1.0 );
	amp *= 1.0 - calm + calm * smoothstep( -aspect, 0.3, p.x );

	// Seed picks the generators' frequencies and how fast their phase shifts
	// from line to line; whole multiples of Phase 1 keep the loop seamless.
	vec3 freq   = mix( vec3( 1.5 ), vec3( 6.0 ), vec3( Hash( vec2( 1.0, 1.0 ) ), Hash( vec2( 2.0, 1.0 ) ), Hash( vec2( 3.0, 1.0 ) ) ) );
	vec3 shift  = mix( 0.2, 1.8, uForm2 ) * mix( vec3( 0.08 ), vec3( 0.5 ), vec3( Hash( vec2( 1.0, 2.0 ) ), Hash( vec2( 2.0, 2.0 ) ), Hash( vec2( 3.0, 2.0 ) ) ) );
	vec3 drift  = TAU * uPhase1 * vec3( 1.0, -1.0, 2.0 );
	vec3 weight = vec3( 0.5, 0.3, 0.2 );

	// Only lines whose largest swing can reach this pixel are visited.
	float reach = abs( amp ) + ( gHalfWidth + 1.0 ) * gPixel;
	float first = clamp( floor( ( p.y - reach + 1.0 ) / spacing - 0.5 ), 0.0, count - 1.0 );
	float last  = clamp( ceil( ( p.y + reach + 1.0 ) / spacing - 0.5 ), 0.0, count - 1.0 );

	// The phase grows by the same step from line to line, so sine and cosine
	// are advanced by rotation instead of being recomputed for every line.
	vec3 a  = freq * p.x + shift * first + drift;
	vec3 sn = sin( a ), cs = cos( a );
	vec3 rs = sin( shift ), rc = cos( shift );

	float best = 1e9, bestId = 0.0;
	for( int i = 0; i < 128; ++i )
	{
		float fk = first + float( i );
		if( fk > last )
			break;
		float y0  = -1.0 + ( fk + 0.5 ) * spacing;
		float env = exp( -pow( ( y0 - centre ) / band, 2.0 ) ) * amp;
		if( abs( p.y - y0 ) <= abs( env ) + ( gHalfWidth + 1.0 ) * gPixel )
		{
			float y     = y0 + env * dot( weight, sn );
			float slope = env * dot( weight * freq, cs );
			float d     = abs( p.y - y ) * inversesqrt( 1.0 + slope * slope );
			if( d < best )
			{
				best   = d;
				bestId = fk;
			}
		}
		vec3 nextSn = sn * rc + cs * rs;
		cs          = cs * rc - sn * rs;
		sn          = nextSn;
	}
	return DistLine( best, gPixel, p.x, bestId );
}

// Op art: horizontal lines swelling round one to four lenses. Form 1 sets
// how strongly they swell, Form 2 the lens size, Form 3 how many lenses.
// Phase 1 sends the lenses once round a circle, Phase 2 makes the swelling
// breathe.
LineInfo WavesBulge( vec2 p )
{
	float lenses   = 1.0 + floor( uForm3 * 3.99 );
	float strength = mix( 0.35, 0.97, uForm1 ) * ( 1.0 + 0.25 * sin( TAU * uPhase2 ) ) * ( 1.0 + HitPulse( length( p ) ) );
	float size     = mix( 0.2, 0.9, uForm2 );
	float orbit    = lenses < 1.5 ? 0.3 : 0.6;
	vec2 pp        = p;
	for( int i = 0; i < 4; ++i )
	{
		if( float( i ) >= lenses )
			break;
		float ang = TAU * ( uPhase1 + float( i ) / lenses ) + Hash( vec2( 5.0, 1.0 ) ) * TAU;
		vec2 c    = orbit * vec2( cos( ang ), sin( ang ) );
		vec2 v    = pp - c;
		// Inside the lens the lines spread apart (the frame is magnified),
		// so outside it they crowd together: the classic op-art swelling.
		float bump = exp( -dot( v, v ) / ( size * size ) );
		pp         = c + v * ( 1.0 - min( strength, 0.97 ) * bump * ( 2.0 - bump ) );
	}
	return LevelLine( pp.y * uCells * 0.5 );
}

// A square mesh pushed around by ripples from one to five sources. Form 1
// sets the wavelength, Form 2 how many sources, Form 3 how far they are from
// the middle. Phase 1 sends the ripples out by one wavelength, Phase 2 lets
// the sources orbit.
LineInfo WavesMesh( vec2 q )
{
	float wavelength = mix( 1.5, 10.0, uForm1 );
	int sources      = 1 + int( uForm2 * 4.99 );
	vec2 d           = vec2( 0.0 );
	for( int i = 0; i < 5; ++i )
	{
		if( i >= sources )
			break;
		float fi  = float( i );
		float dir = Hash( vec2( fi, 2.0 ) ) < 0.5 ? -1.0 : 1.0;
		float ang = fi * TAU / float( sources ) + Hash( vec2( fi, 4.0 ) ) * TAU + TAU * uPhase2 * dir;
		vec2 c    = vec2( cos( ang ), sin( ang ) ) * uCells * mix( 0.0, 0.5, uForm3 );
		vec2 v    = q - c;
		float r   = length( v );
		// Faded in near the source, where the push direction is undefined.
		d += v / max( r, 1e-3 ) * sin( TAU * ( r / wavelength - uPhase1 ) ) * min( r / wavelength, 1.0 );
	}
	vec2 w     = q + d * ( 0.75 + HitPulse( length( q ) * 2.0 / uCells ) * 1.8 ) / sqrt( float( sources ) );
	LineInfo y = LevelLine( w.y );
	y.id      += 4096.0;
	return Nearer( LevelLine( w.x ), y );
}

// Two sets of rings whose centres circle each other: moire. Form 1 sets the
// spacing of the second set against the first, Form 2 how far apart the
// centres are, Form 3 turns the rings into squares. Phase 1 sends the
// centres round, Phase 2 lets one set flow out and the other in.
LineInfo WavesMoire( vec2 q )
{
	float sep  = uForm2 * uCells * 0.3;
	vec2 orbit = uCells * 0.06 * vec2( cos( TAU * uPhase1 ), sin( TAU * uPhase1 ) );
	vec2 v1    = q - vec2( -sep, 0.0 ) - orbit;
	vec2 v2    = q - vec2( sep, 0.0 ) + orbit;
	float d1   = mix( length( v1 ), max( abs( v1.x ), abs( v1.y ) ), uForm3 );
	float d2   = mix( length( v2 ), max( abs( v2.x ), abs( v2.y ) ), uForm3 );
	float hit  = HitPulse( length( q ) * 2.0 / uCells ) * 0.5;
	LineInfo b = LevelLine( d2 / mix( 0.7, 1.3, uForm1 ) + uPhase2 - hit );
	b.id      += 4096.0;
	return Nearer( LevelLine( d1 - uPhase2 + hit ), b );
}

// One Chladni figure: the still lines of a square plate vibrating in mode (n, m).
// Form 1 raises how high the mode numbers go.
float ChladniField( vec2 u, float k )
{
	float n   = 1.0 + floor( Hash( vec2( k, 11.0 ) ) * mix( 2.0, 7.0, uForm1 ) );
	float m   = n + 1.0 + floor( Hash( vec2( k, 23.0 ) ) * mix( 1.0, 4.0, uForm1 ) );
	float sym = Hash( vec2( k, 31.0 ) ) < 0.5 ? -1.0 : 1.0;
	return cos( n * PI * u.x ) * cos( m * PI * u.y ) + sym * cos( m * PI * u.x ) * cos( n * PI * u.y );
}

// Chladni figures. Form 1 sets the complexity, Form 2 above the middle adds
// level lines next to the still ones, Form 3 warps the plate. Phase 1 morphs
// through four modes and back to the first, Phase 2 turns the plate by a
// quarter (which looks the same). Each hit moves on one mode.
LineInfo WavesChladni( vec2 p )
{
	vec2 u     = Rot( p, 0.5 * PI * uPhase2 ) * uCells / 10.0;
	float warp = ( uForm3 - 0.5 ) * 0.5;
	u += warp * vec2( sin( u.y * 2.3 + 1.0 ), sin( u.x * 1.9 + 2.0 ) );
	float s = uPhase1 * 4.0;
	if( uHitCount > 0.5 && uHitAmount > 0.02 )
		s += uHitCount - 1.0 + smoothstep( 0.0, 0.6, uHitAge );
	float a = floor( s );
	float t = smoothstep( 0.35, 1.0, s - a );
	float f = mix( ChladniField( u, mod( a, 4.0 ) ), ChladniField( u, mod( a + 1.0, 4.0 ) ), t );
	// |f| stays below 2, so this scale shows the still lines (f = 0) and as
	// many level lines on each side as Form 2 asks for.
	float levels = floor( max( uForm2 * 2.0 - 1.0, 0.0 ) * 4.99 );
	return LevelLine( f * ( levels + 0.49 ) * 0.5 );
}
)"
R"(
// ---------------------------------------------------------------- main

// Mirror folds the frame before the pattern is drawn, so every pattern
// becomes symmetric (the kaleidoscopes fold it into wedges).
vec2 Fold( vec2 p )
{
	if( uMirror == 1 )
		p.x = abs( p.x );
	else if( uMirror == 2 )
		p.y = abs( p.y );
	else if( uMirror == 3 )
		p = abs( p );
	else if( uMirror >= 4 )
	{
		float n      = uMirror == 4 ? 6.0 : ( uMirror == 5 ? 8.0 : 12.0 );
		float sector = TAU / n;
		float a      = mod( atan( p.y, p.x ), sector );
		a            = abs( a - 0.5 * sector );
		p            = length( p ) * vec2( cos( a ), sin( a ) );
	}
	return p;
}

void main()
{
	vec2 resolution = uViewport.zw;
	gPixel          = 2.0 / resolution.y;
	gHalfWidth      = 0.5 * uWidth;
	gScreen         = ( ( gl_FragCoord.xy - uViewport.xy ) - 0.5 * resolution ) * gPixel;

	vec2 folded = Fold( gScreen );
	vec2 p      = Rot( folded, -uRotation );
	// Warp bends the whole frame with smooth noise (skipped when off: it costs
	// two noise fields per pixel).
	vec2 warp = vec2( 0.0 );
	if( uWarp > 0.0 )
	{
		float size = mix( 0.5, 6.0, uWarpSize );
		warp       = uWarp * 0.35 * vec2( Fbm( p * size + 3.1, 2, 0.5 ), Fbm( p * size + 7.7, 2, 0.5 ) );
	}
	p += warp;
	vec2 q       = p * uCells * 0.5;
	float pxCell = gPixel * uCells * 0.5;

	LineInfo l = NoLine();
#if FAMILY == 0
	if( uVariant == 0 ) l = TilesArcs( q, pxCell );
	else if( uVariant == 1 ) l = TilesDiagonals( q, pxCell );
	else if( uVariant == 2 ) l = TilesHexArcs( q, pxCell );
	else l = TilesSquares( q, pxCell );
#elif FAMILY == 1
	if( uVariant == 0 ) l = RadialSpiral( p );
	else if( uVariant == 1 ) l = RadialBrokenRings( p );
	else if( uVariant == 2 ) l = RadialRays( p );
	else if( uVariant == 3 ) l = RadialPolygons( p );
	else l = RadialRosette( p );
#elif FAMILY == 3
	if( uVariant == 0 ) l = IsoWireCubes( q, pxCell );
	else if( uVariant == 1 ) l = IsoHatchedCubes( q, pxCell );
	else l = IsoLattice( q );
#elif FAMILY == 4
	if( uVariant == 0 ) l = FieldFlowLines( q );
	else if( uVariant == 1 ) l = FieldContours( q );
	else if( uVariant == 2 ) l = FieldTerrain( folded + warp );// Rotation steers the camera instead
	else l = FieldCircles( q, pxCell );
#elif FAMILY == 5
	if( uVariant == 0 ) l = WavesWaves( q );
	else if( uVariant == 1 ) l = WavesResonance( p );
	else if( uVariant == 2 ) l = WavesBulge( p );
	else if( uVariant == 3 ) l = WavesMesh( q );
	else if( uVariant == 4 ) l = WavesMoire( q );
	else l = WavesChladni( p );
#endif
	// Family 2 (Curves) is drawn as geometry on top of this paper.

	float ink = Shade( l );
	fragColor = uInk * ink + uPaper * ( 1.0 - ink );
}
)";

// Curves: Chords, Lissajous and Spirograph, drawn as geometry because
// hundreds of long lines would be far too slow to test per pixel. Each line
// is a run of thin quads (two triangles, six vertices) made from gl_VertexID
// alone, so no buffers are needed; gl_InstanceID draws the Mirror copies.
// The plugin works out all the shape values (DrawCurves in PatternBook.cpp).
static const char kCurveVS[] = R"(#version 410 core
uniform vec4 uViewport;  // x, y, width, height in pixels
uniform int uCurve;      // 0 chords, 1 lissajous, 2 spirograph
uniform float uLines;    // chords, or copies of the curve
uniform float uSegments; // segments per line
uniform float uLength;   // rough length of one line, for dashes
uniform float uShift;    // chords: inner circle turn; lissajous: phase; spirograph: copies turning against each other
uniform float uFactor, uInner, uSpan, uTwist;// chords
uniform vec2 uFreq;      // lissajous
uniform float uSpread;   // lissajous: phase step between copies
uniform float uSpin;     // lissajous and spirograph
uniform vec3 uWheel;     // spirograph: wheel radius, pen distance, turns
uniform float uLobes, uSize;// spirograph
uniform float uRotation; // radians
uniform float uWidth;    // line width in pixels
uniform int uMirror;
out float vAcross;       // distance from the line's centre, in pixels
out float vAlong;        // position along the line, frame units
flat out float vId;

const float TAU = 6.28318530717959;

vec2 Rot( vec2 v, float a )
{
	float c = cos( a ), s = sin( a );
	return vec2( c * v.x - s * v.y, s * v.x + c * v.y );
}

// Point t (0..1) along line number `line`.
vec2 CurvePoint( float line, float t )
{
	if( uCurve == 0 )
	{
		// Point i at angle a on the outer circle joins angle k * a on an inner circle.
		float a   = TAU * uSpan * line / uLines;
		float b   = uFactor * a + uShift + uTwist;
		vec2 from = 0.85 * vec2( cos( a ), sin( a ) );
		vec2 to   = uInner * 0.85 * vec2( cos( b ), sin( b ) );
		return mix( from, to, t );
	}
	if( uCurve == 1 )
	{
		// A Lissajous figure in 3D, spun round the vertical axis and seen from the front.
		float s = TAU * t;
		float d = uShift + line * uSpread;
		vec3 c  = vec3( sin( uFreq.x * s + d ), sin( uFreq.y * s ), cos( uFreq.x * s + d ) );
		return 0.8 * vec2( c.x * cos( uSpin ) + c.z * sin( uSpin ) * 0.6, c.y );
	}
	// Spirograph (hypotrochoid): a wheel of radius r rolls inside a ring of
	// radius 1 for `turns` turns; the pen sits at distance pen from its centre.
	// uShift turns every other copy against the rest by up to one lobe, so
	// the copies weave through each other and a full cycle looks the same.
	float r     = uWheel.x;
	float s     = TAU * uWheel.z * t;
	float w     = ( 1.0 - r ) / r * s;
	vec2 pt     = vec2( ( 1.0 - r ) * cos( s ) + uWheel.y * cos( w ), ( 1.0 - r ) * sin( s ) - uWheel.y * sin( w ) );
	float weave = ( mod( line, 2.0 ) < 0.5 ? 1.0 : -1.0 ) * uShift / uLobes;
	return Rot( pt, TAU * line / ( uLines * uLobes ) + weave + uSpin ) * uSize;
}

// Mirror copy number `copy`: reflections, or turns and reflections for the kaleidoscopes.
vec2 MirrorCopy( vec2 p, int copy )
{
	if( uMirror == 1 && copy == 1 )
		p.x = -p.x;
	else if( uMirror == 2 && copy == 1 )
		p.y = -p.y;
	else if( uMirror == 3 )
		p *= vec2( ( copy & 1 ) == 1 ? -1.0 : 1.0, ( copy & 2 ) == 2 ? -1.0 : 1.0 );
	else if( uMirror >= 4 )
	{
		float turns = uMirror == 4 ? 3.0 : ( uMirror == 5 ? 4.0 : 6.0 );
		if( ( copy & 1 ) == 1 )
			p.y = -p.y;
		p = Rot( p, TAU * float( copy / 2 ) / turns );
	}
	return p;
}

void main()
{
	int corner  = gl_VertexID % 6;
	float index = float( gl_VertexID / 6 );
	float line  = floor( index / uSegments );
	float seg   = index - line * uSegments;
	float t0    = seg / uSegments, t1 = ( seg + 1.0 ) / uSegments;
	vec2 from   = Rot( MirrorCopy( CurvePoint( line, t0 ), gl_InstanceID ), uRotation );
	vec2 to     = Rot( MirrorCopy( CurvePoint( line, t1 ), gl_InstanceID ), uRotation );

	// Corners of the quad: (along the segment 0..1, side -1..1).
	const vec2 corners[ 6 ] = vec2[ 6 ]( vec2( 0.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( 1.0, 1.0 ),
										 vec2( 0.0, -1.0 ), vec2( 1.0, 1.0 ), vec2( 0.0, 1.0 ) );
	vec2 c       = corners[ corner ];
	float pixel  = 2.0 / uViewport.w;
	float reach  = 0.5 * uWidth + 1.0;// half width plus one pixel for anti-aliasing
	vec2 dir     = to - from;
	float len    = length( dir );
	dir          = len > 1e-6 ? dir / len : vec2( 1.0, 0.0 );
	vec2 normal  = vec2( -dir.y, dir.x );
	vec2 pos     = mix( from, to, c.x ) + dir * ( c.x * 2.0 - 1.0 ) * reach * pixel * 0.5 + normal * c.y * reach * pixel;
	vAcross      = c.y * reach;
	vAlong       = uCurve == 0 ? c.x * len : mix( t0, t1, c.x ) * uLength;
	vId          = line;
	float aspect = uViewport.z / uViewport.w;
	gl_Position  = vec4( pos.x / aspect, pos.y, 0.0, 1.0 );
}
)";

static const char kCurveFS[] = R"(#version 410 core
uniform vec4 uViewport;
uniform float uWidth;
uniform float uDash, uDashLength, uDashShift, uThin;
uniform float uSeed;
uniform vec4 uInk; // premultiplied
in float vAcross;
in float vAlong;
flat in float vId;
out vec4 fragColor;

float Hash( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.x + p3.y ) * p3.z );
}

void main()
{
	if( uThin > 0.0 && Hash( vec2( vId, 91.7 ) ) < uThin )
		discard;
	float d = abs( vAcross );
	if( uDash > 0.0 )
	{
		float period = uDashLength;
		float duty   = 1.0 - 0.9 * uDash;
		float w      = fract( vAlong / period + uDashShift ) * period;
		float gap    = w > duty * period ? min( w - duty * period, period - w ) : 0.0;
		d            = length( vec2( d, gap / ( 2.0 / uViewport.w ) ) );
	}
	fragColor = uInk * clamp( 0.5 * uWidth - d + 0.5, 0.0, 1.0 );
}
)";
}// namespace pattern
