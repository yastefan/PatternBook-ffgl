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
static const char kPatternFS[] = R"(#version 410 core
uniform vec4 uViewport;  // x, y, width, height in pixels
uniform int uFamily;     // 0 grid, 1 radial, 2 isometric, 3 distortion
uniform int uVariant;    // 0..3 within the family
uniform float uSeed;
uniform float uCells;    // cells across the frame height
uniform float uForm;     // pattern-specific, 0..1
uniform float uWidth;    // line width in pixels
uniform float uRotation; // radians
uniform float uPhase;
uniform float uMotion;
uniform float uAmount;   // 0..1
uniform float uHitCount;
uniform float uHitAge;
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

// Quarter-turn count from hits for something at distance dist (frame units)
// from the centre: each hit adds one turn as its wave passes.
float HitTurns( float dist )
{
	if( uHitCount < 0.5 )
		return 0.0;
	float progress = clamp( ( uHitAge * HIT_SPEED - dist ) / 0.35, 0.0, 1.0 );
	return uHitCount - 1.0 + smoothstep( 0.0, 1.0, progress );
}

// A ring of energy travelling out from the centre after a hit, fading out.
float HitPulse( float dist )
{
	if( uHitCount < 0.5 )
		return 0.0;
	float x = ( dist - uHitAge * HIT_SPEED ) / 0.18;
	return exp( -x * x ) * exp( -uHitAge * 0.9 );
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
	float turns = step( 0.5, Hash( cell ) ) + CellTurns( cell ) + HitTurns( dist );
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
	float wave = sin( TAU * uPhase - dot( cell, vec2( 0.35, 0.25 ) ) );
	float a    = ( uAmount * wave + HitTurns( dist ) ) * 0.5 * PI;
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
	float grow   = mix( 1.0, 0.5 + 0.5 * sin( TAU * ( uPhase + h ) ), uAmount );
	float outer  = inner + ( 1.7 - inner ) * grow + 0.4 * HitPulse( r );
	float onRay  = r * cos( da );
	float beside = abs( r * sin( da ) );
	float past   = max( max( inner - onRay, onRay - outer ), 0.0 );
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
		float turns = CellTurns( key ) + HitTurns( dist );
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
		g += uAmount * 0.5 * sin( TAU * uPhase + fi * TAU / 3.0 );
		g += uForm * 0.35 * sin( dot( q, t ) * 1.3 + TAU * uPhase + fi );
		g += HitPulse( length( q ) * 2.0 / uCells ) * 0.5;
		ink = max( ink, Lines( g ) );
	}
	return ink;
}

// ---------------------------------------------------------------- distortion

// A square mesh pushed around by waves from three slowly orbiting sources.
float DistortInterference( vec2 q )
{
	float wavelength = mix( 2.0, 10.0, uForm );
	vec2 d           = vec2( 0.0 );
	for( int i = 0; i < 3; ++i )
	{
		float fi  = float( i );
		float ang = fi * TAU / 3.0 + uMotion * TAU * 0.15 * ( i == 1 ? -1.0 : 1.0 );
		vec2 c    = vec2( cos( ang ), sin( ang ) ) * uCells * 0.3;
		vec2 v    = q - c;
		float r   = length( v );
		// Faded in near the source, where the push direction is undefined.
		d += v / max( r, 1e-3 ) * sin( TAU * ( r / wavelength - uPhase ) ) * min( r / wavelength, 1.0 );
	}
	vec2 w = q + d * ( uAmount * 0.35 + HitPulse( length( q ) * 2.0 / uCells ) * 0.6 );
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

// ---------------------------------------------------------------- main

void main()
{
	vec2 resolution = uViewport.zw;
	gPixel          = 2.0 / resolution.y;
	gHalfWidth      = 0.5 * uWidth;
	vec2 p          = ( ( gl_FragCoord.xy - uViewport.xy ) - 0.5 * resolution ) * gPixel;
	p               = Rot( p, -uRotation );
	vec2 q          = p * uCells * 0.5;
	float pxCell    = gPixel * uCells * 0.5;

	float ink = 0.0;
	if( uFamily == 0 )
	{
		if( uVariant == 0 ) ink = GridTiles( q, pxCell, 0 );
		else if( uVariant == 1 ) ink = GridTiles( q, pxCell, 1 );
		else if( uVariant == 2 ) ink = GridSquares( q, pxCell );
		else ink = GridDots( q, pxCell );
	}
	else if( uFamily == 1 )
	{
		if( uVariant == 0 ) ink = RadialSpiral( p );
		else if( uVariant == 1 ) ink = RadialRings( p );
		else if( uVariant == 2 ) ink = RadialRays( p );
		else ink = RadialDots( p );
	}
	else if( uFamily == 2 )
	{
		if( uVariant == 3 ) ink = IsoLattice( q );
		else ink = IsoCubes( q, pxCell, uVariant );
	}
	else
	{
		if( uVariant == 0 ) ink = DistortInterference( q );
		else if( uVariant == 1 ) ink = DistortRipple( q );
		else if( uVariant == 2 ) ink = DistortWaves( q );
		else ink = DistortMoire( q );
	}

	fragColor = uInk * ink + uPaper * ( 1.0 - ink );
}
)";
}// namespace pattern
