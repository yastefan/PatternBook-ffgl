// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <FFGLSDK.h>
#include <vector>

// Resolume source that draws minimal generative line patterns: tiles, radial
// figures, string-art curves, isometric wireframes, noise fields and wave
// fields. Each pattern has three Form controls for its shape and two Phase
// controls that loop seamlessly, so Resolume's own parameter animation can
// drive them; Drift runs Phase 1 on its own in time with the BPM. A shared
// Lines section dashes, thins, mirrors and warps every pattern alike.
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
	void DrawCurves();
	float Phase1() const;// Phase 1 plus everything Drift has added, 0..1

	static const int kFamilyCount = 6;
	ffglex::FFGLShader shaders[ kFamilyCount ];// one program per pattern family
	ffglex::FFGLShader curveShader;            // Curves, drawn as line geometry
	GLuint emptyVAO  = 0;// core profile needs a bound VAO even without attributes
	GLuint heightMap = 0;// tileable landscape for Field: Terrain

	// Parameters, stored as the host's values.
	int pattern      = 0;
	int seed         = 0;
	float scale      = 0.43f;
	float forms[ 3 ] = { 0.5f, 0.5f, 0.5f };
	float width      = 0.25f;
	float rotation   = 0.5f;
	float phases[ 2 ] = { 0.0f, 0.0f };
	float drift      = 0.3f;
	float hitAmount  = 0.5f;
	int hitEvery     = 0;// 0 = only the Hit button
	bool hitPressed  = false;
	bool hitPending  = false;
	float dash       = 0.0f;
	float dashLength = 0.3f;
	float dashShift  = 0.0f;
	float thin       = 0.0f;
	int mirror       = 0;
	float warp       = 0.0f;
	float warpSize   = 0.4f;
	float ink[ 4 ]   = { 0.0f, 0.0f, 1.0f, 1.0f };// hue, saturation, brightness, alpha
	float paper[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	// State.
	double lastTime     = -1.0;
	double barPosition  = 0.0;// bars since the plugin started, following the host
	float lastBarPhase  = -1.0f;
	bool hostSendsBeats = false;
	double drifted      = 0.0;// cycles Drift has added to Phase 1 (0..1)
	double hitCount     = 0.0;
	double hitAge       = 1000.0;// seconds since the last hit
	double flyX = 0.0, flyZ = 0.0;// Field: Terrain camera, flying where Rotation points
	float cameraHeight = 0.0f;   // glides after the valley floor
	bool cameraReady   = false;
	std::vector< float > heightValues;// the terrain height map, kept to steer the camera
};
