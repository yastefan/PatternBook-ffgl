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

// Every pattern is computed per pixel as "ink coverage" between 0 (paper) and
// 1 (ink), anti-aliased over about one pixel.
//
// Coordinates: the frame's height spans -1..1 ("frame units"), x scales with
// the aspect ratio. Most patterns work in cell units, q = p * cells / 2, so
// Scale sets how many cells fit across the frame height.
//
// Animation inputs:
//   uPhase   cycles at the chosen rate; drives waves and turns (Amount sets their size)
//   uMotion  cycles weighted by Amount; drives steady drifts and spins
//   uHitCount, uHitAge  number of hits so far and seconds since the last one;
//            each hit sends a wave out from the centre
// The plugin compiles this once per family, prepending "#version 410 core"
// and "#define FAMILY n", so each program only contains one family's code.
static const char kPatternFS[] = R"(
uniform vec4 uViewport;  // x, y, width, height in pixels
uniform int uVariant;    // pattern within the family
uniform float uSeed;
uniform float uCells;    // cells across the frame height
uniform float uForm;     // pattern-specific, 0..1
uniform float uForm2;    // a second pattern-specific control, 0..1
uniform float uWidth;    // line width in pixels
uniform float uRotation; // radians
uniform float uPhase;
uniform float uMotion;
uniform float uAmount;   // 0..1
uniform int uSway;       // 0: endless motion, 1: sway back and forth (Motion switch)
uniform float uHitCount;
uniform float uHitAge;
uniform float uHitAmount; // 0..1
uniform vec3 uCamera;    // Noise: Terrain camera position (x, height, z)
uniform vec4 uInk;       // premultiplied
uniform vec4 uPaper;     // premultiplied
out vec4 fragColor;

const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;
const float HIT_SPEED = 1.5; // frame units per second

float gPixel;     // one pixel in frame units
float gHalfWidth; // half the line width in pixels

// ---------------------------------------------------------------- helpers

float Hash( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.x + p3.y ) * p3.z );
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

// Coverage of a line at distance d, where px is one pixel in the same units.
float Line( float d, float px ) { return clamp( gHalfWidth - d / px + 0.5, 0.0, 1.0 ); }

// Coverage of a filled shape with signed distance sd (negative inside).
float Fill( float sd, float px ) { return clamp( 0.5 - sd / px, 0.0, 1.0 ); }

// Lines wherever the smooth field g is a whole number. The screen-space
// derivative turns the field distance into pixels, so this works for any warp.
float Lines( float g )
{
	float perPixel = max( length( vec2( dFdx( g ), dFdy( g ) ) ), 1e-6 );
	float d        = abs( g - round( g ) ) / perPixel;
	// Fade lines out where they would be closer than about two pixels, instead of aliasing.
	return clamp( gHalfWidth - d + 0.5, 0.0, 1.0 ) * clamp( 2.0 - 2.0 * perPixel, 0.0, 1.0 );
}

// Lines like Lines(), with every fifth one widened, as on a topographic map.
float IndexLines( float g )
{
	float perPixel = max( length( vec2( dFdx( g ), dFdy( g ) ) ), 1e-6 );
	float d        = abs( g - round( g ) ) / perPixel;
	float halfW    = mod( round( g ), 5.0 ) == 0.0 ? gHalfWidth * 2.0 : gHalfWidth;
	return clamp( halfW - d + 0.5, 0.0, 1.0 ) * clamp( 2.0 - 2.0 * perPixel, 0.0, 1.0 );
}

// A single line where the smooth field f crosses zero.
float ZeroLine( float f )
{
	float perPixel = max( length( vec2( dFdx( f ), dFdy( f ) ) ), 1e-6 );
	return clamp( gHalfWidth - abs( f ) / perPixel + 0.5, 0.0, 1.0 );
}

// ---------------------------------------------------------------- noise

vec2 Hash22( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * vec3( 0.1031, 0.1030, 0.0973 ) );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.xx + p3.yz ) * p3.zy ) * 2.0 - 1.0;
}

vec3 Hash33( vec3 p )
{
	p = fract( ( p + uSeed * vec3( 17.13, 31.71, 7.37 ) ) * vec3( 0.1031, 0.1030, 0.0973 ) );
	p += dot( p, p.yxz + 33.33 );
	return fract( ( p.xxy + p.yxx ) * p.zyx ) * 2.0 - 1.0;
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

// Gradient noise that also evolves along z (used as time).
float Noise( vec3 p )
{
	vec3 i = floor( p ), f = fract( p );
	vec3 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float n[ 8 ];
	for( int k = 0; k < 8; ++k )
	{
		vec3 c = vec3( k & 1, ( k >> 1 ) & 1, ( k >> 2 ) & 1 );
		n[ k ] = dot( Hash33( i + c ), f - c );
	}
	return mix( mix( mix( n[ 0 ], n[ 1 ], u.x ), mix( n[ 2 ], n[ 3 ], u.x ), u.y ),
				mix( mix( n[ 4 ], n[ 5 ], u.x ), mix( n[ 6 ], n[ 7 ], u.x ), u.y ), u.z );
}

float Fbm( vec3 p, int octaves )
{
	float sum = 0.0, amp = 0.5;
	for( int i = 0; i < octaves; ++i )
	{
		sum += amp * Noise( p );
		p = vec3( mat2( 1.6, 1.2, -1.2, 1.6 ) * p.xy, p.z * 1.4 );// turned to hide the grid
		amp *= 0.5;
	}
	return sum * 2.0;
}

float Fbm( vec2 p, int octaves )
{
	float sum = 0.0, amp = 0.5;
	for( int i = 0; i < octaves; ++i )
	{
		sum += amp * Noise( p );
		p = mat2( 1.6, 1.2, -1.2, 1.6 ) * p;
		amp *= 0.5;
	}
	return sum * 2.0;
}

// Quarter-turn count from hits for the cell at distance dist (frame units)
// from the centre: each hit turns a Hit Amount share of the cells by one, as
// its wave passes.
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

// Turns of one cell: an Amount share of the cells makes one quick quarter
// turn per cycle, each at its own moment.
float CellTurns( vec2 cell )
{
	if( Hash( cell + 13.7 ) >= uAmount )
		return 0.0;
	float s = uPhase + Hash( cell + 71.3 );
	return floor( s ) + smoothstep( 0.72, 1.0, fract( s ) );
}

// ---------------------------------------------------------------- grid

// Quarter-circle arcs (kind 0) or diagonals (kind 1) in each cell, turned by
// a hash, by the animation and by hits.
float GridTiles( vec2 q, float px, int kind )
{
	vec2 cell   = floor( q );
	vec2 f      = q - cell - 0.5;
	float dist  = length( cell + 0.5 ) * 2.0 / uCells;
	float turns = step( 0.5, Hash( cell ) ) + CellTurns( cell ) + HitTurns( dist, cell );
	f           = Rot( f, turns * 0.5 * PI );

	if( kind == 0 )
	{
		// Form widens the single arc into a ribbon with two edges.
		float band = uForm * 0.2;
		float r1   = abs( length( f - 0.5 ) - 0.5 );
		float r2   = abs( length( f + 0.5 ) - 0.5 );
		float d    = min( abs( r1 - band ), abs( r2 - band ) );
		return Line( d, px );
	}
	// Form adds parallel lines; their spacing divides the cell edge, so
	// neighbouring tiles always meet.
	float k = 1.0 + floor( uForm * 4.99 );
	float t = ( f.x - f.y ) * k;
	return Line( abs( t - round( t ) ) / k * 0.70710678, px );
}

// Square outlines turning in a diagonal wave.
float GridSquares( vec2 q, float px )
{
	vec2 cell  = floor( q );
	vec2 f     = q - cell - 0.5;
	float dist = length( cell + 0.5 ) * 2.0 / uCells;
	float turn;
	if( uSway == 1 )// turn back and forth in a diagonal wave
		turn = uAmount * sin( TAU * uPhase - dot( cell, vec2( 0.35, 0.25 ) ) );
	else// keep turning, each cell a little behind its neighbour; a square looks the same after a quarter turn
		turn = 2.0 * uMotion - 0.12 * ( cell.x + cell.y );
	float a    = ( turn + HitTurns( dist, cell ) ) * 0.5 * PI;
	float size = mix( 0.1, 0.35, uForm );// at most 0.35, so a turned square stays inside its cell
	vec2 d     = abs( Rot( f, a ) ) - size;
	float sd   = length( max( d, 0.0 ) ) + min( max( d.x, d.y ), 0.0 );
	return Line( abs( sd ), px );
}

// Halftone dots breathing in a wave from the centre.
float GridDots( vec2 q, float px )
{
	vec2 cell  = floor( q );
	vec2 f     = q - cell - 0.5;
	float dist = length( cell + 0.5 ) * 2.0 / uCells;
	float wave = 0.5 + 0.5 * sin( TAU * uPhase - dist * 6.0 + ( Hash( cell ) - 0.5 ) * 1.5 );
	float r    = 0.48 * mix( 0.25, 1.0, uForm ) * mix( 1.0, wave, uAmount ) + 0.2 * HitPulse( dist );
	return Fill( length( f ) - min( r, 0.5 ), px );
}

// ---------------------------------------------------------------- radial

// Archimedean spiral with Form arms, turning steadily.
float RadialSpiral( vec2 p )
{
	float r     = length( p );
	float a     = atan( p.y, p.x );
	float n     = 1.0 + floor( uForm * 7.99 );
	float s     = 2.0 / uCells;// distance between neighbouring turns
	float spin  = uMotion * TAU + HitPulse( r ) * 1.5;
	float g     = r / s - n * ( a + spin ) / TAU;
	// Analytic gradient: dFdx would see the jump where atan wraps.
	float grad  = length( vec2( 1.0 / s, n / ( TAU * max( r, 1e-4 ) ) ) );
	return Line( abs( g - round( g ) ) / grad, gPixel );
}

// Concentric rings broken into arcs; every ring spins at its own speed and direction.
float RadialRings( vec2 p )
{
	float s    = 2.0 / uCells;
	float r    = length( p );
	float a    = atan( p.y, p.x );
	float j    = max( round( r / s ), 1.0 );
	float dir  = Hash( vec2( j, 9.0 ) ) < 0.5 ? -1.0 : 1.0;
	float spin = dir * ( 0.2 + 0.8 * Hash( vec2( j, 3.0 ) ) ) * uMotion * TAU * 0.25;
	float segs = 1.0 + floor( Hash( vec2( j, 5.0 ) ) * 5.0 );
	float u    = fract( ( a + spin ) / TAU * segs );
	float gap  = clamp( uForm * 0.6 + HitPulse( r ) * 0.4, 0.0, 0.95 );
	// Distance past the end of the arc, measured along the ring.
	float along  = max( abs( u - 0.5 ) - 0.5 * ( 1.0 - gap ), 0.0 ) * TAU * r / segs;
	float across = abs( r - j * s );
	return Line( length( vec2( across, along ) ), gPixel );
}

// Rays from an inner radius, each growing and shrinking at its own phase.
float RadialRays( vec2 p )
{
	float r      = length( p );
	float a      = atan( p.y, p.x );
	float n      = max( round( uCells * 3.0 ), 3.0 );
	float u      = a / TAU * n + uMotion * 0.5;
	float i      = round( u );
	float da     = ( u - i ) / n * TAU;// angle to the nearest ray
	float h      = Hash( vec2( mod( i, n ), 1.0 ) );
	float inner  = mix( 0.05, 0.7, uForm );
	float onRay  = r * cos( da );
	float beside = abs( r * sin( da ) );
	if( uSway == 1 )
	{
		// Each ray grows and shrinks at its own phase.
		float grow  = mix( 1.0, 0.5 + 0.5 * sin( TAU * ( uPhase + h ) ), uAmount );
		float outer = inner + ( 1.7 - inner ) * grow + 0.4 * HitPulse( r );
		float past  = max( max( inner - onRay, onRay - outer ), 0.0 );
		return Line( length( vec2( beside, past ) ), gPixel );
	}
	// Endless: dashes run outwards along every ray; Amount opens the gaps.
	float outer  = 1.7 + 0.4 * HitPulse( r );
	float period = 0.35;
	float duty   = 1.0 - 0.7 * uAmount;
	float w      = fract( ( onRay - inner ) / period - 2.0 * uMotion - h ) * period;// position within one dash period
	float gap    = w > duty * period ? min( w - duty * period, period - w ) : 0.0;// distance to the nearest dash
	float past   = max( max( max( inner - onRay, onRay - outer ), 0.0 ), gap );
	return Line( length( vec2( beside, past ) ), gPixel );
}

// Rings of evenly spaced dots, turning in alternate directions so the rings
// slide past each other.
float RadialDots( vec2 p )
{
	float s    = 2.0 / uCells;
	float r    = length( p );
	float a    = atan( p.y, p.x );
	float j    = round( r / s );
	float dotR = s * mix( 0.08, 0.45, uForm ) * ( 1.0 + 0.8 * HitPulse( r ) );
	if( j < 0.5 )
		return Fill( r - dotR, gPixel );
	float count = round( TAU * j );
	float dir   = mod( j, 2.0 ) < 0.5 ? 1.0 : -1.0;
	float spin  = dir * uMotion * TAU * ( 0.1 + 0.9 / j );
	float k     = round( ( a + spin ) / TAU * count );
	float ca    = k / count * TAU - spin;
	vec2 centre = j * s * vec2( cos( ca ), sin( ca ) );
	return Fill( length( p - centre ) - dotR, gPixel );
}

// ---------------------------------------------------------------- isometric

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

float IsoCubes( vec2 q, float pxCell, int kind )
{
	vec2 P     = q * 1.7320508;// hexagons one cell wide
	vec4 hc    = HexCell( P );
	vec2 g     = hc.xy;
	vec2 key   = round( hc.zw * vec2( 1.1547005, 0.6666667 ) );
	float dist = length( hc.zw ) / 1.7320508 * 2.0 / uCells;
	float px   = pxCell * 1.7320508;

	if( kind == 1 )
	{
		// Wire cubes: the inner corner flips by a sixth turn, so a cube seen
		// from above becomes one seen from below.
		float turns = CellTurns( key ) + HitTurns( dist, key );
		float a     = turns * PI / 3.0;
		float d     = min( abs( HexDistance( g ) ), CubeEdges( g, a ) );
		if( uForm > 0.02 )
		{
			float inner = mix( 0.15, 0.85, uForm );
			vec2 gi     = g / inner;
			d           = min( d, min( abs( HexDistance( gi ) ), CubeEdges( gi, -a ) ) * inner );
		}
		return Line( d, px );
	}

	// Solid and hatched cubes shrink and grow in a wave from the centre.
	// For solid cubes Form scatters the cells' timing.
	float scatter = kind == 0 ? Hash( key ) * uForm * TAU : 0.0;
	float wave    = 0.5 + 0.5 * sin( TAU * uPhase - dist * 5.0 + scatter );
	float scale = 1.0 - 0.75 * uAmount * wave;
	scale       = max( scale * ( 1.0 - 0.5 * HitPulse( dist ) ), 0.05 );
	vec2 gs     = g / scale;
	float sd    = HexDistance( gs ) * scale;
	float face  = CubeFace( gs );
	float edges = max( Line( CubeEdges( gs, 0.0 ) * scale, px ), Line( abs( sd ), px ) );

	if( kind == 0 )
	{
		// Three shades; the edges are drawn in paper colour to keep the faces apart.
		float shade = face < 0.5 ? 0.35 : ( face < 1.5 ? 1.0 : 0.62 );
		return Fill( sd, px ) * shade * ( 1.0 - edges );
	}

	// Hatched: shading by vertical lines, light from the upper left. The top
	// face stays empty, the right face gets twice the lines of the left.
	float density = ( 2.0 + floor( uForm * 6.0 ) ) * ( face < 0.5 ? 2.0 : 1.0 );
	float t       = gs.x * density;
	float hatch   = face > 0.5 && face < 1.5 ? 0.0 : Line( abs( t - round( t ) ) / density * scale, px );
	return Fill( sd, px ) * max( hatch, edges );
}

// Three families of parallel lines on the isometric axes, sliding and
// weaving against each other.
float IsoLattice( vec2 q )
{
	float ink = 0.0;
	for( int i = 0; i < 3; ++i )
	{
		float fi  = float( i );
		float ang = PI * 0.5 + fi * TAU / 3.0;
		vec2 n    = vec2( cos( ang ), sin( ang ) );
		vec2 t    = vec2( -n.y, n.x );
		float g   = dot( q, n ) * 1.1547005;// lines one cell apart
		if( uSway == 1 )
			g += uAmount * 0.5 * sin( TAU * uPhase + fi * TAU / 3.0 );
		else// the three families glide on endlessly, the middle one against the others
			g += uMotion * ( i == 1 ? -1.0 : 1.0 );
		g += uForm * 0.35 * sin( dot( q, t ) * 1.3 + TAU * uPhase + fi );
		g += HitPulse( length( q ) * 2.0 / uCells ) * 0.5;
		ink = max( ink, Lines( g ) );
	}
	return ink;
}

// Height of the column on base b (hexagon units) in Iso: Noise Heights.
float ColumnHeight( vec2 b, float hmax )
{
	float dist = length( b ) / 1.7320508 * 2.0 / uCells;
	vec2 drift = vec2( uMotion * 0.3, -uMotion * 0.21 );
	float n    = clamp( 0.5 + 0.9 * Noise( b * 0.2 + drift ), 0.0, 1.0 ) * hmax;
	// Whole cubes, so the heights read as terraces; the step between two
	// heights is quick but smooth.
	float h = floor( n ) + smoothstep( 0.75, 1.0, fract( n ) );
	return max( h + 1.5 * HitPulse( dist ), 0.05 );
}

// Columns of isometric cubes whose heights follow a slowly changing noise
// landscape. Columns nearer the viewer (lower on screen) hide those behind,
// so candidates are visited front to back until the pixel is covered.
float IsoHeights( vec2 q, float pxCell )
{
	vec2 P     = q * 1.7320508;
	float px   = pxCell * 1.7320508;
	float hmax = 1.0 + uForm * 5.0;
	float m0   = round( HexCell( P ).w / 1.5 );// row of the hexagon under this pixel
	int below  = int( ceil( ( hmax + 2.5 ) / 1.5 ) ) + 1;
	float ink  = 0.0;
	float left = 1.0;// share of the pixel not yet covered by a nearer column
	for( int j = -9; j <= 1; ++j )
	{
		if( j < -below )
			continue;
		float m   = m0 + float( j );
		float off = mod( m, 2.0 ) > 0.5 ? 0.8660254 : 0.0;
		float xc  = round( ( P.x - off ) / 1.7320508 ) * 1.7320508 + off;
		float side = P.x > xc ? 1.0 : -1.0;// a column is one hexagon wide, so two per row can reach this pixel
		for( int s = 0; s < 2; ++s )
		{
			vec2 b   = vec2( xc + float( s ) * side * 1.7320508, m * 1.5 );
			float H  = ColumnHeight( b, hmax );
			vec2 l   = P - b;
			float sd = HexDistance( l - vec2( 0.0, clamp( l.y, 0.0, H ) ) );
			float cover = Fill( sd, px );
			if( cover <= 0.0 )
				continue;
			vec2 tl     = l - vec2( 0.0, H );
			bool top    = HexDistance( tl ) <= 0.0 && CubeFace( tl ) > 0.5 && CubeFace( tl ) < 1.5;
			float shade = top ? 1.0 : ( l.x < 0.0 ? 0.62 : 0.35 );
			float edges = min( min( CubeEdges( tl, 0.0 ), Segment( l, vec2( 0.0, H ), vec2( 0.0, -1.0 ) ) ), abs( sd ) );
			ink += left * cover * shade * ( 1.0 - Line( edges, px ) );
			left *= 1.0 - cover;
		}
		if( left < 0.004 )
			break;
	}
	return ink;
}

// ---------------------------------------------------------------- distortion

// A square mesh pushed around by waves from two to five slowly orbiting
// sources; Seed picks how many and where.
float DistortInterference( vec2 q )
{
	float wavelength = mix( 2.0, 10.0, uForm );
	int sources      = 2 + int( Hash( vec2( 3.1, 7.7 ) ) * 3.99 );
	vec2 d           = vec2( 0.0 );
	for( int i = 0; i < 5; ++i )
	{
		if( i >= sources )
			break;
		float fi  = float( i );
		float dir = Hash( vec2( fi, 2.0 ) ) < 0.5 ? -1.0 : 1.0;
		float ang = fi * TAU / float( sources ) + Hash( vec2( fi, 4.0 ) ) + uMotion * TAU * 0.15 * dir;
		vec2 c    = vec2( cos( ang ), sin( ang ) ) * uCells * mix( 0.15, 0.45, Hash( vec2( fi, 6.0 ) ) );
		vec2 v    = q - c;
		float r   = length( v );
		// Faded in near the source, where the push direction is undefined.
		d += v / max( r, 1e-3 ) * sin( TAU * ( r / wavelength - uPhase ) ) * min( r / wavelength, 1.0 );
	}
	vec2 w = q + d * ( uAmount * 0.5 + HitPulse( length( q ) * 2.0 / uCells ) * 0.6 ) * 3.0 / float( sources );
	return max( Lines( w.x ), Lines( w.y ) );
}

// A square mesh with ripples running out from the centre.
float DistortRipple( vec2 q )
{
	float wavelength = mix( 1.5, 8.0, uForm );
	float r          = length( q );
	vec2 dir         = q / max( r, 1e-3 );
	float push       = uAmount * 0.8 * sin( TAU * ( r / wavelength - uPhase ) );
	push += HitPulse( r * 2.0 / uCells ) * 1.2;
	// Calm the centre, where the push direction is undefined.
	vec2 w = q + dir * push * min( r / wavelength, 1.0 );
	return max( Lines( w.x ), Lines( w.y ) );
}

// Horizontal lines rippling like fabric.
float DistortWaves( vec2 q )
{
	float k = mix( 0.15, 1.2, uForm );
	float g = q.y;
	g += uAmount * 0.8 * sin( q.x * k + TAU * uPhase ) * cos( q.y * k * 0.5 - TAU * uPhase * 0.5 );
	g += uAmount * 0.3 * sin( q.x * k * 2.3 - q.y * k * 0.7 - TAU * uPhase );
	g += HitPulse( length( q ) * 2.0 / uCells ) * 0.8;
	return Lines( g );
}

// Two sets of concentric rings whose centres circle each other: moire.
float DistortMoire( vec2 q )
{
	float sep  = uForm * uCells * 0.3;
	vec2 orbit = uAmount * 2.0 * vec2( cos( TAU * uPhase ), sin( TAU * uPhase ) );
	vec2 c1    = vec2( -sep, 0.0 ) + orbit;
	vec2 c2    = vec2( sep, 0.0 ) - orbit;
	float hit  = HitPulse( length( q ) * 2.0 / uCells ) * 0.5;
	return max( Lines( length( q - c1 ) + hit ), Lines( length( q - c2 ) - hit ) );
}

// One Chladni figure: the still lines of a square plate vibrating in mode (n, m).
// Form raises how high the mode numbers go.
float ChladniField( vec2 u, float k )
{
	float n   = 1.0 + floor( Hash( vec2( k, 11.0 ) ) * mix( 2.0, 7.0, uForm ) );
	float m   = n + 1.0 + floor( Hash( vec2( k, 23.0 ) ) * mix( 1.0, 4.0, uForm ) );
	float sym = Hash( vec2( k, 31.0 ) ) < 0.5 ? -1.0 : 1.0;
	return cos( n * PI * u.x ) * cos( m * PI * u.y ) + sym * cos( m * PI * u.x ) * cos( n * PI * u.y );
}

// Chladni figures that morph from one vibration mode to the next once per
// cycle; Amount is how long the morph takes, each hit moves on one mode.
float DistortChladni( vec2 p )
{
	vec2 u   = p * uCells / 10.0;
	float s  = floor( uPhase ) + smoothstep( 1.0 - uAmount, 1.0, fract( uPhase ) );
	if( uHitCount > 0.5 && uHitAmount > 0.02 )
		s += uHitCount - 1.0 + smoothstep( 0.0, 0.6, uHitAge );
	float a  = floor( s );
	float t  = smoothstep( 0.0, 1.0, s - a );
	float f  = mix( ChladniField( u, a ), ChladniField( u, a + 1.0 ), t );
	return ZeroLine( f );
}

// Many horizontal lines, each bent by the same few sine generators but with
// a phase that shifts from line to line, so neighbouring lines fan out and
// cross where the generators resonate. Lines are drawn one by one (not as
// level lines of one field), which is what lets them cross.
float DistortResonance( vec2 p )
{
	float count   = clamp( round( uCells * 3.0 ), 8.0, 128.0 );
	float spacing = 2.0 / count;
	float band    = mix( 0.1, 0.9, uForm );// half-height of the vibrating band
	float centre  = ( Hash( vec2( 8.0, 1.0 ) ) - 0.5 ) * 0.4;
	float amp     = uAmount * 0.9 + 0.5 * HitPulse( abs( p.x ) );// a hit runs out sideways from the middle
	// The swing grows from the left edge, so the lines leave the left side
	// almost calm and tangle further right.
	float aspect = uViewport.z / uViewport.w;
	amp *= 0.2 + 0.8 * smoothstep( -aspect, 0.3, p.x );

	// Three generators; Seed picks their frequencies and how fast the phase
	// shifts from line to line.
	vec3 freq  = mix( vec3( 1.5 ), vec3( 6.0 ), vec3( Hash( vec2( 1.0, 1.0 ) ), Hash( vec2( 2.0, 1.0 ) ), Hash( vec2( 3.0, 1.0 ) ) ) );
	vec3 shift = mix( vec3( 0.08 ), vec3( 0.5 ), vec3( Hash( vec2( 1.0, 2.0 ) ), Hash( vec2( 2.0, 2.0 ) ), Hash( vec2( 3.0, 2.0 ) ) ) );
	vec3 drift = TAU * uMotion * vec3( 1.0, -0.7, 0.45 );
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

	float ink = 0.0;
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
			ink = max( ink, Line( abs( p.y - y ) * inversesqrt( 1.0 + slope * slope ), gPixel ) );
		}
		vec3 nextSn = sn * rc + cs * rs;
		cs          = cs * rc - sn * rs;
		sn          = nextSn;
	}
	return ink;
}

// ---------------------------------------------------------------- noise family

// Streams: lines that run across the frame and bend around slowly turning
// eddies. They are level lines of a stream function, so they never cross.
float NoiseFlow( vec2 q )
{
	float r   = length( q ) * 2.0 / uCells;
	vec2 s    = Rot( q, HitPulse( r ) * 1.5 );// a hit twists the streams
	float amp = mix( 0.6, 6.0, uForm );
	return Lines( s.y + amp * Fbm( vec3( s * 0.09, uMotion * 0.35 ), 3 ) );
}

// Topographic contour lines of a drifting noise landscape, every fifth one heavier.
float NoiseContours( vec2 q )
{
	float levels = mix( 3.0, 16.0, uForm );
	float r      = length( q ) * 2.0 / uCells;
	float h      = Fbm( vec3( q * 0.07, uMotion * 0.25 ), 3 ) + 0.5 * HitPulse( r );
	return IndexLines( h * levels * 2.5 );
}

// The landscape comes from a tileable height map the plugin builds once:
// a texture lookup per ray step is far cheaper than noise. Seed shifts it.
// Along the line the camera flies, the mountains sink into a valley, so the
// camera flies through them rather than over them. The plugin samples the
// same map to keep the camera just above the valley floor (TerrainFloor in
// PatternBook.cpp must match).
uniform sampler2D uHeightMap;
float TerrainHeight( vec2 xz, float amp )
{
	float h       = amp * textureLod( uHeightMap, xz * 0.55 / 16.0 + uSeed * vec2( 0.37, 0.71 ), 0.0 ).r;
	vec2 across   = vec2( cos( uRotation ), -sin( uRotation ) );// sideways to the flight
	float lateral = dot( xz - uCamera.xz, across ) / ( 1.4 + 0.3 * amp );
	return h * ( 1.0 - 0.85 * exp( -lateral * lateral ) );
}

// A wireframe landscape in perspective that the viewer flies through.
// Rotation turns the camera (and the flight) instead of the picture.
float NoiseTerrain( vec2 p )
{
	float amp = 0.1 + 4.0 * uForm * uForm;// from gentle hills to high mountains
	if( uHitCount > 0.5 )
		amp *= 1.0 + 2.4 * uHitAmount * exp( -uHitAge * 2.0 );// a hit makes the mountains jump
	vec3 ro  = uCamera;
	vec3 rd  = normalize( vec3( p.x, p.y - 0.05, 1.4 ) );
	rd.xz    = Rot( rd.xz, -uRotation );
	float far = 30.0;

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
		if( t > far )
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
	float ink  = max( Lines( pos.x * dens ), Lines( pos.z * dens ) );
	// Fade into the distance, where steep far-off slopes would only show as fringes.
	return hit ? ink * exp( -b * 0.13 ) : 0.0;
}

// Scattered, overlapping circles with concentric rings that drift and breathe.
float NoiseCircles( vec2 q, float pxCell )
{
	const float S = 2.0;// cells per circle site
	vec2 cell     = floor( q / S );
	float rings   = 1.0 + floor( uForm * 3.99 );
	float ink     = 0.0;
	// Radius and drift are capped so that only the 3 x 3 nearest sites can reach this pixel.
	for( int y = -1; y <= 1; ++y )
		for( int x = -1; x <= 1; ++x )
		{
			vec2 id = cell + vec2( x, y );
			if( Hash( id + 5.1 ) > 0.7 )
				continue;
			float h  = Hash( id + 1.3 );
			vec2 jit = 0.25 * vec2( sin( TAU * ( uMotion * 0.15 + h ) ), cos( TAU * ( uMotion * 0.12 + Hash( id + 2.7 ) ) ) );
			vec2 c   = ( id + 0.5 + jit ) * S;
			float R  = S * ( 0.3 + 0.6 * Hash( id + 3.9 ) ) * ( 1.0 + 0.25 * uAmount * sin( TAU * ( uPhase + h ) ) );
			R        = min( R * ( 1.0 + 0.5 * HitPulse( length( c ) * 2.0 / uCells ) ), 0.95 * S );
			float rr = length( q - c );
			float sp = R / rings;
			float j  = clamp( round( rr / sp ), 1.0, rings );
			ink      = max( ink, Line( abs( rr - j * sp ), pxCell ) );
		}
	return ink;
}

// ---------------------------------------------------------------- main

void main()
{
	vec2 resolution = uViewport.zw;
	gPixel          = 2.0 / resolution.y;
	gHalfWidth      = 0.5 * uWidth;
	vec2 p          = ( ( gl_FragCoord.xy - uViewport.xy ) - 0.5 * resolution ) * gPixel;
	vec2 screen     = p;// unrotated, for the terrain camera
	p               = Rot( p, -uRotation );
	vec2 q          = p * uCells * 0.5;
	float pxCell    = gPixel * uCells * 0.5;

	float ink = 0.0;
#if FAMILY == 0
	{
		if( uVariant == 0 ) ink = GridTiles( q, pxCell, 0 );
		else if( uVariant == 1 ) ink = GridTiles( q, pxCell, 1 );
		else if( uVariant == 2 ) ink = GridSquares( q, pxCell );
		else ink = GridDots( q, pxCell );
	}
#elif FAMILY == 1
	{
		if( uVariant == 0 ) ink = RadialSpiral( p );
		else if( uVariant == 1 ) ink = RadialRings( p );
		else if( uVariant == 2 ) ink = RadialRays( p );
		else if( uVariant == 3 ) ink = RadialDots( p );
		// Variant 4 (Chords) is drawn as geometry over this paper; see kChordVS.
	}
#elif FAMILY == 2
	{
		if( uVariant == 3 ) ink = IsoLattice( q );
		else if( uVariant == 4 ) ink = IsoHeights( q, pxCell );
		else ink = IsoCubes( q, pxCell, uVariant );
	}
#elif FAMILY == 3
	{
		if( uVariant == 0 ) ink = DistortInterference( q );
		else if( uVariant == 1 ) ink = DistortRipple( q );
		else if( uVariant == 2 ) ink = DistortWaves( q );
		else if( uVariant == 3 ) ink = DistortMoire( q );
		else if( uVariant == 4 ) ink = DistortChladni( p );
		else ink = DistortResonance( p );
	}
#else
	{
		if( uVariant == 0 ) ink = NoiseFlow( q );
		else if( uVariant == 1 ) ink = NoiseContours( q );
		else if( uVariant == 2 ) ink = NoiseTerrain( screen );
		else ink = NoiseCircles( q, pxCell );
	}
#endif

	fragColor = uInk * ink + uPaper * ( 1.0 - ink );
}
)";
// Radial: Chords, drawn as geometry because hundreds of long lines would be
// far too slow to test per pixel. Point i at angle a on the outer circle
// joins angle k * a on an inner circle; each line is a thin quad (two
// triangles, six vertices) made from gl_VertexID alone, so no buffers are
// needed. The plugin computes k with its animation (ChordFactor in
// PatternBook.cpp) and draws the paper first with the radial family shader.
static const char kChordVS[] = R"(#version 410 core
uniform vec4 uViewport;  // x, y, width, height in pixels
uniform float uCount;    // number of lines
uniform float uFactor;   // k
uniform float uOffset;   // turn of the inner circle against the outer one, radians
uniform float uInner;    // inner radius relative to the outer one
uniform float uRotation; // radians
uniform float uWidth;    // line width in pixels
out float vAcross;       // distance from the line's centre, in pixels

const float TAU = 6.28318530717959;
const float R   = 0.85; // outer radius in frame units (frame height spans -1..1)

vec2 Rot( vec2 v, float a )
{
	float c = cos( a ), s = sin( a );
	return vec2( c * v.x - s * v.y, s * v.x + c * v.y );
}

void main()
{
	int line   = gl_VertexID / 6;
	int corner = gl_VertexID % 6;
	float a    = TAU * float( line ) / uCount;
	float b    = uFactor * a + uOffset;
	vec2 from  = Rot( R * vec2( cos( a ), sin( a ) ), uRotation );
	vec2 to    = Rot( uInner * R * vec2( cos( b ), sin( b ) ), uRotation );

	// Corners of the quad: (along the line 0..1, side -1..1).
	const vec2 corners[ 6 ] = vec2[ 6 ]( vec2( 0.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( 1.0, 1.0 ),
										 vec2( 0.0, -1.0 ), vec2( 1.0, 1.0 ), vec2( 0.0, 1.0 ) );
	vec2 c        = corners[ corner ];
	float pixel   = 2.0 / uViewport.w;
	float reach   = 0.5 * uWidth + 1.0;// half width plus one pixel for anti-aliasing
	vec2 dir      = to - from;
	float len     = length( dir );
	dir           = len > 1e-6 ? dir / len : vec2( 1.0, 0.0 );
	vec2 normal   = vec2( -dir.y, dir.x );
	vec2 pos      = mix( from, to, c.x ) + dir * ( c.x * 2.0 - 1.0 ) * reach * pixel * 0.5 + normal * c.y * reach * pixel;
	vAcross       = c.y * reach;
	float aspect  = uViewport.z / uViewport.w;
	gl_Position   = vec4( pos.x / aspect, pos.y, 0.0, 1.0 );
}
)";

static const char kChordFS[] = R"(#version 410 core
uniform float uWidth;
uniform vec4 uInk; // premultiplied
in float vAcross;
out vec4 fragColor;
void main()
{
	fragColor = uInk * clamp( 0.5 * uWidth - abs( vAcross ) + 0.5, 0.0, 1.0 );
}
)";
}// namespace pattern
