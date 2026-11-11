// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <FFGLSDK.h>

// Resolume source that draws minimal generative patterns (grids, radial
// figures, isometric cubes, distorted meshes) and animates them in time with
// the host's tempo. Everything is computed per pixel in one shader, so lines
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

	ffglex::FFGLShader shader;
	GLuint emptyVAO = 0;// core profile needs a bound VAO even without attributes

	// Parameters, stored as the host's values.
	int pattern    = 0;
	int seed       = 0;
	float scale    = 0.43f;
	float form     = 0.5f;
	float width    = 0.25f;
	float rotation = 0.5f;
	int rate       = 3;
	float amount   = 0.5f;
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
	double phase        = 0.0;// animation cycles at the chosen rate
	double motion       = 0.0;// cycles weighted by Amount, for drifts that must not jump when Amount moves
	double hitCount     = 0.0;
	double hitAge       = 1000.0;// seconds since the last hit
};
