// SPDX-License-Identifier: GPL-3.0-or-later
// Offscreen FFGL host for testing PatternBook without Resolume.
//
// Loads the plugin bundle, drives it through the same plugMain calls a host
// makes, renders into an FBO on a headless OpenGL 4.1 core context and writes
// PNG frames. A Debug build of the plugin asserts on any GL state it leaks.
//
// Example:
//   patternbook-harness --plugin build/PatternBook.bundle --out renders \
//       --bpm 120 --frames 480 --every 30 --set Pattern=4 --at 240 Hit=1

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreServices/CoreServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <ffgl/FFGL.h>

namespace
{
typedef FFMixed ( *PlugMainFn )( FFUInt32, FFMixed, FFInstanceID );
typedef void ( *SetLogCallbackFn )( void ( * )( const char* ) );

PlugMainFn gPlugMain = nullptr;

struct Action
{
	int frame;
	std::string name;
	std::string value;
};

void Fail( const char* message )
{
	fprintf( stderr, "harness: %s\n", message );
	exit( 1 );
}

void LogFromPlugin( const char* message )
{
	printf( "[plugin] %s\n", message );
}

FFMixed Call( FFUInt32 code, FFMixed input, FFInstanceID instance = nullptr )
{
	return gPlugMain( code, input, instance );
}

FFMixed FromUInt( FFUInt32 value )
{
	FFMixed mixed;
	mixed.PointerValue = nullptr;
	mixed.UIntValue    = value;
	return mixed;
}

FFMixed FromPointer( const void* pointer )
{
	FFMixed mixed;
	mixed.PointerValue = const_cast< void* >( pointer );
	return mixed;
}

struct ParamInfo
{
	FFUInt32 index;
	FFUInt32 type;
};

std::map< std::string, ParamInfo > ListParams()
{
	std::map< std::string, ParamInfo > params;
	FFUInt32 count = Call( FF_GET_NUM_PARAMETERS, FromUInt( 0 ) ).UIntValue;
	for( FFUInt32 i = 0; i < count; ++i )
	{
		const char* name = (const char*)Call( FF_GET_PARAMETER_NAME, FromUInt( i ) ).PointerValue;
		FFUInt32 type    = Call( FF_GET_PARAMETER_TYPE, FromUInt( i ) ).UIntValue;
		if( name )
			params[ name ] = { i, type };
	}
	return params;
}

void SetParam( FFInstanceID instance, const std::map< std::string, ParamInfo >& params, const std::string& name, const std::string& value )
{
	auto it = params.find( name );
	if( it == params.end() )
	{
		fprintf( stderr, "harness: unknown parameter '%s'. Known:", name.c_str() );
		for( const auto& entry : params )
			fprintf( stderr, " '%s'", entry.first.c_str() );
		fprintf( stderr, "\n" );
		exit( 1 );
	}
	SetParameterStruct set;
	set.ParameterNumber = it->second.index;
	if( it->second.type == FF_TYPE_TEXT || it->second.type == FF_TYPE_FILE )
	{
		set.NewParameterValue = FromPointer( value.c_str() );
	}
	else
	{
		float number = std::stof( value );
		FFUInt32 bits;
		memcpy( &bits, &number, sizeof( bits ) );
		set.NewParameterValue = FromUInt( bits );
	}
	if( Call( FF_SET_PARAMETER, FromPointer( &set ), instance ).UIntValue != FF_SUCCESS )
		fprintf( stderr, "harness: plugin rejected %s=%s\n", name.c_str(), value.c_str() );
}

void WritePNG( const std::string& path, int width, int height )
{
	std::vector< unsigned char > pixels( (size_t)width * height * 4 );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	// GL rows start at the bottom; images start at the top.
	std::vector< unsigned char > flipped( pixels.size() );
	size_t rowBytes = (size_t)width * 4;
	for( int y = 0; y < height; ++y )
		memcpy( &flipped[ y * rowBytes ], &pixels[ ( height - 1 - y ) * rowBytes ], rowBytes );

	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
	CGDataProviderRef provider = CGDataProviderCreateWithData( nullptr, flipped.data(), flipped.size(), nullptr );
	// Ignore alpha: the plugin output is premultiplied, so this shows it over black like Resolume does.
	CGImageRef image = CGImageCreate( width, height, 8, 32, rowBytes, colorSpace, kCGImageAlphaNoneSkipLast, provider, nullptr, false, kCGRenderingIntentDefault );
	NSURL* url       = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
	CGImageDestinationRef destination = CGImageDestinationCreateWithURL( (__bridge CFURLRef)url, CFSTR( "public.png" ), 1, nullptr );
	CGImageDestinationAddImage( destination, image, nullptr );
	if( !CGImageDestinationFinalize( destination ) )
		fprintf( stderr, "harness: failed to write %s\n", path.c_str() );
	CFRelease( destination );
	CGImageRelease( image );
	CGDataProviderRelease( provider );
	CGColorSpaceRelease( colorSpace );
}

// One CSV row per frame, to measure motion and output cleanliness numerically:
// lit = pixels above black, partial = lit pixels below full ink brightness
// mean = average
// level of the lit pixels, cx/cy = brightness-weighted centroid in pixels.
void WriteStats( FILE* file, int frame, int width, int height )
{
	std::vector< unsigned char > pixels( (size_t)width * height * 4 );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	int peak = 0;
	for( size_t i = 0; i < pixels.size(); i += 4 )
		peak = std::max( peak, (int)std::max( { pixels[ i ], pixels[ i + 1 ], pixels[ i + 2 ] } ) );
	long long lit = 0, partial = 0;
	double sum = 0.0, sx = 0.0, sy = 0.0;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const unsigned char* px = &pixels[ ( (size_t)y * width + x ) * 4 ];
			int level               = std::max( { px[ 0 ], px[ 1 ], px[ 2 ] } );
			if( level == 0 )
				continue;
			++lit;
			if( level < peak - 2 )
				++partial;
			sum += level;
			sx += level * x;
			sy += level * ( height - 1 - y );
		}
	fprintf( file, "%d,%lld,%lld,%d,%.3f,%.3f,%.3f\n", frame, lit, partial, peak, lit ? sum / lit : 0.0, sum > 0 ? sx / sum : 0.0, sum > 0 ? sy / sum : 0.0 );
}

void Usage()
{
	printf(
		"usage: patternbook-harness --plugin PATH.bundle [options]\n"
		"  --out DIR            directory for PNG frames (default: renders)\n"
		"  --size WxH           render size (default: 960x540)\n"
		"  --frames N           frames to render (default: 240)\n"
		"  --fps N              simulated frame rate (default: 60)\n"
		"  --every N            write every Nth frame (default: 30; 0 = only the last)\n"
		"  --set NAME=VALUE     set a parameter before the first frame\n"
		"  --at FRAME NAME=VALUE  set a parameter at a frame (repeatable)\n"
		"  --bpm N              send the host tempo and bar phase each frame, as Resolume does (4/4)\n"
		"  --throughput         don't sync after each frame; only the wall time is meaningful\n"
		"  --stale-gl-error     leave a GL error pending before instantiating, like Resolume sometimes does\n"
		"  --stats FILE         write per-frame CSV: lit pixels, mean level, centroid, partial (anti-aliased) pixels\n"
		"  --list               print the plugin's parameters and exit\n" );
}

bool SplitAssignment( const std::string& text, std::string& name, std::string& value )
{
	size_t equals = text.find( '=' );
	if( equals == std::string::npos )
		return false;
	name  = text.substr( 0, equals );
	value = text.substr( equals + 1 );
	return true;
}
}// namespace

int main( int argc, char** argv )
{
	@autoreleasepool
	{
		std::string pluginPath, outDir = "renders";
		int width = 960, height = 540, frames = 240, fps = 60, every = 30;
		std::string statsPath;
		bool listOnly   = false;
		bool throughput = false;
		double bpm      = 0.0;
		bool staleError = false;
		std::vector< Action > actions;

		for( int i = 1; i < argc; ++i )
		{
			std::string arg = argv[ i ];
			auto next       = [ & ]() -> std::string {
                if( i + 1 >= argc )
                    Fail( ( "missing value for " + arg ).c_str() );
                return argv[ ++i ];
			};
			std::string name, value;
			if( arg == "--plugin" ) pluginPath = next();
			else if( arg == "--out" ) outDir = next();
			else if( arg == "--size" ) { if( sscanf( next().c_str(), "%dx%d", &width, &height ) != 2 ) Fail( "bad --size" ); }
			else if( arg == "--frames" ) frames = std::stoi( next() );
			else if( arg == "--fps" ) fps = std::stoi( next() );
			else if( arg == "--every" ) every = std::stoi( next() );
			else if( arg == "--stats" ) statsPath = next();
			else if( arg == "--bpm" ) bpm = std::stod( next() );
			else if( arg == "--list" ) listOnly = true;
			else if( arg == "--throughput" ) throughput = true;
			else if( arg == "--stale-gl-error" ) staleError = true;
			else if( arg == "--set" )
			{
				if( !SplitAssignment( next(), name, value ) ) Fail( "--set expects NAME=VALUE" );
				actions.push_back( { 0, name, value } );
			}
			else if( arg == "--at" )
			{
				int frame = std::stoi( next() );
				if( !SplitAssignment( next(), name, value ) ) Fail( "--at expects FRAME NAME=VALUE" );
				actions.push_back( { frame, name, value } );
			}
			else { Usage(); return arg == "--help" ? 0 : 1; }
		}
		if( pluginPath.empty() )
		{
			Usage();
			return 1;
		}

		// Headless OpenGL 4.1 core context, the same profile Resolume gives plugins on macOS.
		CGLPixelFormatAttribute attributes[] = {
			kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
			kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers, (CGLPixelFormatAttribute)0
		};
		CGLPixelFormatObj pixelFormat;
		GLint numFormats;
		if( CGLChoosePixelFormat( attributes, &pixelFormat, &numFormats ) != kCGLNoError || !pixelFormat )
			Fail( "no OpenGL 4.1 core pixel format" );
		CGLContextObj context;
		if( CGLCreateContext( pixelFormat, nullptr, &context ) != kCGLNoError )
			Fail( "cannot create OpenGL context" );
		CGLDestroyPixelFormat( pixelFormat );
		CGLSetCurrentContext( context );
		printf( "GL: %s | %s\n", glGetString( GL_RENDERER ), glGetString( GL_VERSION ) );

		std::string binary = pluginPath + "/Contents/MacOS/PatternBook";
		void* library      = dlopen( binary.c_str(), RTLD_NOW | RTLD_LOCAL );
		if( !library )
			Fail( dlerror() );
		gPlugMain = (PlugMainFn)dlsym( library, "plugMain" );
		if( !gPlugMain )
			Fail( "plugMain not exported" );
		if( auto setLog = (SetLogCallbackFn)dlsym( library, "SetLogCallback" ) )
			setLog( LogFromPlugin );

		if( Call( FF_INITIALISE_V2, FromUInt( 0 ) ).UIntValue != FF_SUCCESS )
			Fail( "FF_INITIALISE_V2 failed" );
		std::map< std::string, ParamInfo > params = ListParams();
		if( listOnly )
		{
			for( const auto& entry : params )
				printf( "%3u  type %-4u %s\n", entry.second.index, entry.second.type, entry.first.c_str() );
			return 0;
		}

		GLuint texture, fbo;
		glGenTextures( 1, &texture );
		glBindTexture( GL_TEXTURE_2D, texture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			Fail( "framebuffer incomplete" );
		glViewport( 0, 0, width, height );

		if( staleError )
			glBindTexture( GL_TEXTURE_2D, 0xBAD );// unknown name in core profile: GL_INVALID_OPERATION, left unread

		FFGLViewportStruct viewport = { 0, 0, (GLuint)width, (GLuint)height };
		FFInstanceID instance       = Call( FF_INSTANTIATE_GL, FromPointer( &viewport ) ).PointerValue;
		if( instance == nullptr || instance == (FFInstanceID)(uintptr_t)FF_FAIL )
			Fail( "FF_INSTANTIATE_GL failed" );


		FILE* statsFile = nullptr;
		if( !statsPath.empty() )
		{
			statsFile = fopen( statsPath.c_str(), "w" );
			if( !statsFile )
				Fail( "cannot write --stats file" );
			fprintf( statsFile, "frame,lit,partial,peak,mean,cx,cy\n" );
		}

		NSError* error = nil;
		[[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:outDir.c_str()] withIntermediateDirectories:YES attributes:nil error:&error];

		ProcessOpenGLStruct process = {};
		process.HostFBO             = fbo;
		std::vector< double > frameMs;
		auto loopStart = std::chrono::steady_clock::now();
		for( int frame = 0; frame < frames; ++frame )
		{
			for( const Action& action : actions )
				if( action.frame == frame )
					SetParam( instance, params, action.name, action.value );

			double time = frame * 1000.0 / fps;
			Call( FF_SET_TIME, FromPointer( &time ), instance );
			if( bpm > 0.0 )
			{
				double bars        = time / 1000.0 * bpm / 240.0;
				SetBeatinfoStruct beat;
				beat.bpm      = static_cast< float >( bpm );
				beat.barPhase = static_cast< float >( bars - std::floor( bars ) );
				Call( FF_SET_BEATINFO, FromPointer( &beat ), instance );
			}

			auto start = std::chrono::steady_clock::now();
			if( Call( FF_PROCESS_OPENGL, FromPointer( &process ), instance ).UIntValue != FF_SUCCESS )
				Fail( "FF_PROCESS_OPENGL failed" );
			if( !throughput )
				glFinish();
			frameMs.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );

			GLenum glError = glGetError();
			if( glError != GL_NO_ERROR )
			{
				fprintf( stderr, "harness: GL error 0x%04x at frame %d\n", glError, frame );
				return 1;
			}

			if( statsFile )
				WriteStats( statsFile, frame, width, height );

			bool last = frame == frames - 1;
			if( last || ( every > 0 && frame % every == 0 ) )
			{
				char file[ 64 ];
				snprintf( file, sizeof( file ), "/frame_%04d.png", frame );
				WritePNG( outDir + file, width, height );
			}
		}
		glFinish();
		if( statsFile )
			fclose( statsFile );
		printf( "Wall time: %.2f ms/frame (pipelined throughput with --throughput)\n", std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - loopStart ).count() / frames );
		// The first frame includes the driver's lazy shader compilation and buffer uploads, so report the median.
		if( throughput )
			return 0;
		std::vector< double > sorted = frameMs;
		std::sort( sorted.begin(), sorted.end() );
		printf( "Rendered %d frames at %dx%d: median %.2f ms, first %.2f ms, max %.2f ms per frame (CPU + GPU, synced)\n",
				frames, width, height, sorted[ sorted.size() / 2 ], frameMs.front(), sorted.back() );

		Call( FF_DEINSTANTIATE_GL, FromUInt( 0 ), instance );
		Call( FF_DEINITIALISE, FromUInt( 0 ) );
		glDeleteFramebuffers( 1, &fbo );
		glDeleteTextures( 1, &texture );
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
	}
	return 0;
}
