// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <FFGLSDK.h>
#include <vector>

// Resolume source that draws minimal generative patterns (grids, radial
// figures, isometric cubes, distorted meshes) and animates them in time with
// the host's tempo. Everything is computed per pixel in a shader, so lines
// stay sharp at any output resolution.
class PatternBook : public CFFGLPlugin
{
public:
	PatternBook();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult DeInitGL() override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void UpdateClock( float deltaTime );
	void UpdateCamera( float deltaTime );
	void DrawChords();

	static const int kFamilyCount = 5;
	ffglex::FFGLShader shaders[ kFamilyCount ];// one program per pattern family
	ffglex::FFGLShader chordShader;// Radial: Chords, drawn as lines
	GLuint emptyVAO  = 0;// core profile needs a bound VAO even without attributes
	GLuint heightMap = 0;// tileable landscape for Noise: Terrain

	// Parameters, stored as the host's values.
	int pattern    = 0;
	int seed       = 0;
	float scale    = 0.43f;
	float form     = 0.5f;
	float form2    = 0.5f;
	float form3    = 0.5f;
	float width    = 0.25f;
	float rotation = 0.5f;
	float speed    = 0.35f;
	float amount   = 0.5f;
	float hitAmount = 0.5f;
	int motionMode  = 0;// 0 endless, 1 sway
	int hitEvery   = 0;// 0 = only the Hit button
	bool hitPressed = false;
	bool hitPending = false;
	float ink[ 4 ]   = { 0.0f, 0.0f, 1.0f, 1.0f };// hue, saturation, brightness, alpha
	float paper[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	// State.
	double lastTime     = -1.0;
	double barPosition  = 0.0;// bars since the plugin started, following the host
	float lastBarPhase  = -1.0f;
	bool hostSendsBeats = false;
	double phase        = 0.0;// animation cycles at the chosen speed
	double motion       = 0.0;// cycles weighted by Amount, for drifts that must not jump when Amount moves
	double hitCount     = 0.0;
	double hitAge       = 1000.0;// seconds since the last hit
	double flyX = 0.0, flyZ = 0.0;// Noise: Terrain camera position, flying where Rotation points
	float cameraHeight = 0.0f;// glides after the valley floor
	bool cameraReady   = false;
	std::vector< float > heightValues;// the terrain height map, kept to steer the camera
};
