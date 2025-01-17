#include <stdlib.h> // EXIT_FAILURE, EXIT_SUCCESS

#include <glib.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <shoveler/camera/perspective.h>
#include <shoveler/drawable/quad.h>
#include <shoveler/material/canvas.h>
#include <shoveler/material/tile_sprite.h>
#include <shoveler/material/tilemap.h>
#include <shoveler/canvas.h>
#include <shoveler/colliders.h>
#include <shoveler/constants.h>
#include <shoveler/controller.h>
#include <shoveler/framebuffer.h>
#include <shoveler/game.h>
#include <shoveler/global.h>
#include <shoveler/image.h>
#include <shoveler/input.h>
#include <shoveler/material.h>
#include <shoveler/model.h>
#include <shoveler/opengl.h>
#include <shoveler/scene.h>
#include <shoveler/shader_program.h>
#include <shoveler/sprite/tile.h>
#include <shoveler/sprite/tilemap.h>
#include <shoveler/texture.h>
#include <shoveler/tilemap.h>
#include <shoveler/tile_sprite_animation.h>
#include <shoveler/tileset.h>
#include <shoveler/types.h>

static void updateGame(ShovelerGame *game, double dt);

static ShovelerSprite *characterSprite;
static ShovelerTileSpriteAnimation *animation;

int main(int argc, char *argv[])
{
	ShovelerGameWindowSettings windowSettings;
	windowSettings.windowTitle = "shoveler";
	windowSettings.fullscreen = false;
	windowSettings.vsync = true;
	windowSettings.samples = 4;
	windowSettings.windowedWidth = 640;
	windowSettings.windowedHeight = 480;

	ShovelerGameCameraSettings cameraSettings;
	cameraSettings.frame.position = shovelerVector3(0, 0, 10);
	cameraSettings.frame.direction = shovelerVector3(0, 0, -1);
	cameraSettings.frame.up = shovelerVector3(0, 1, 0);
	cameraSettings.projection.fieldOfViewY = 2.0f * SHOVELER_PI * 50.0f / 360.0f;
	cameraSettings.projection.aspectRatio = (float) windowSettings.windowedWidth / windowSettings.windowedHeight;
	cameraSettings.projection.nearClippingPlane = 0.01;
	cameraSettings.projection.farClippingPlane = 1000;

	ShovelerGameControllerSettings controllerSettings;
	controllerSettings.frame = cameraSettings.frame;
	controllerSettings.moveFactor = 5.0f;
	controllerSettings.tiltFactor = 0.0005f;
	controllerSettings.boundingBoxSize2 = 0.5f;
	controllerSettings.boundingBoxSize3 = 0.0f;

	shovelerLogInit("shoveler/", SHOVELER_LOG_LEVEL_INFO_UP, stdout);
	shovelerGlobalInit();

	ShovelerGame *game = shovelerGameCreate(updateGame, &windowSettings, &cameraSettings, &controllerSettings);
	if(game == NULL) {
		return EXIT_FAILURE;
	}

	game->controller->lockTiltX = true;
	game->controller->lockTiltY = true;

	ShovelerCanvas *canvas = shovelerCanvasCreate(/* numLayers */ 3);

	ShovelerMaterial *tilemapMaterial = shovelerMaterialTilemapCreate(game->shaderCache, /* screenspace */ false);

	ShovelerImage *tilesImage = shovelerImageCreate(2, 2, 3);
	shovelerImageClear(tilesImage);
	shovelerImageGet(tilesImage, 0, 0, 0) = 0;
	shovelerImageGet(tilesImage, 0, 0, 1) = 0;
	shovelerImageGet(tilesImage, 0, 0, 2) = 1; // red
	shovelerImageGet(tilesImage, 0, 1, 0) = 0;
	shovelerImageGet(tilesImage, 0, 1, 1) = 0;
	shovelerImageGet(tilesImage, 0, 1, 2) = 1; // red
	shovelerImageGet(tilesImage, 1, 0, 0) = 0;
	shovelerImageGet(tilesImage, 1, 0, 1) = 1;
	shovelerImageGet(tilesImage, 1, 0, 2) = 1; // green
	shovelerImageGet(tilesImage, 1, 1, 0) = 0;
	shovelerImageGet(tilesImage, 1, 1, 1) = 0;
	shovelerImageGet(tilesImage, 1, 1, 2) = 2; // full tileset
	ShovelerTexture *tilesTexture = shovelerTextureCreate2d(tilesImage, true);
	shovelerTextureUpdate(tilesTexture);
	bool collidingTiles[4] = {false, false, false, true};
	ShovelerTilemap *tilemap = shovelerTilemapCreate(tilesTexture, collidingTiles);
	ShovelerSprite *tilemapSprite = shovelerSpriteTilemapCreate(tilemapMaterial, tilemap);
	tilemapSprite->size = shovelerVector2(10.0f, 10.0f);
	shovelerCanvasAddSprite(canvas, /* layerId */ 0, tilemapSprite);
	shovelerCollidersAddCollider2(game->colliders, &tilemapSprite->collider);

	ShovelerImage *borderTilesImage = shovelerImageCreate(1, 1, 3);
	shovelerImageClear(borderTilesImage);
	shovelerImageGet(borderTilesImage, 0, 0, 0) = 0;
	shovelerImageGet(borderTilesImage, 0, 0, 1) = 0;
	shovelerImageGet(borderTilesImage, 0, 0, 2) = 1; // full tileset
	ShovelerTexture *borderTilesTexture = shovelerTextureCreate2d(borderTilesImage, true);
	shovelerTextureUpdate(borderTilesTexture);
	ShovelerTilemap *borderTilemap = shovelerTilemapCreate(borderTilesTexture, NULL);
	ShovelerSprite *borderTilemapSprite = shovelerSpriteTilemapCreate(tilemapMaterial, borderTilemap);
	borderTilemapSprite->size = shovelerVector2(10.0f, 10.0f);
	shovelerCanvasAddSprite(canvas, /* layerId */ 2, borderTilemapSprite);
	shovelerCollidersAddCollider2(game->colliders, &borderTilemapSprite->collider);

	ShovelerImage *tilesetImage = shovelerImageCreate(2, 2, 3);
	shovelerImageClear(tilesetImage);
	shovelerImageGet(tilesetImage, 0, 0, 0) = 255; // red
	shovelerImageGet(tilesetImage, 0, 1, 1) = 255; // green
	shovelerImageGet(tilesetImage, 1, 0, 2) = 255; // blue
	shovelerImageGet(tilesetImage, 1, 1, 0) = 255;
	shovelerImageGet(tilesetImage, 1, 1, 1) = 255;
	shovelerImageGet(tilesetImage, 1, 1, 2) = 255; // white
	ShovelerTileset *tileset = shovelerTilesetCreate(tilesetImage, 2, 2, 1);
	shovelerTilemapAddTileset(tilemap, tileset);
	ShovelerTileset *tileset2 = shovelerTilesetCreate(tilesetImage, 1, 1, 1);
	shovelerTilemapAddTileset(tilemap, tileset2);

	ShovelerImage *animationTilesetImage = shovelerImageCreateAnimationTileset(tilesetImage, 1);
	ShovelerTileset *animationTileset = shovelerTilesetCreate(animationTilesetImage, 4, 3, 1);

	shovelerImageFree(tilesetImage);
	shovelerImageFree(animationTilesetImage);

	ShovelerImage *borderTilesetImage = shovelerImageCreate(20, 20, 4);
	shovelerImageClear(borderTilesetImage);
	shovelerImageSet(borderTilesetImage, shovelerColor(255, 200, 255), 0);
	shovelerImageAddFrame(borderTilesetImage, 5, shovelerColor(255, 200, 255));
	ShovelerTileset *borderTileset = shovelerTilesetCreate(borderTilesetImage, 1, 1, 1);
	shovelerImageFree(borderTilesetImage);
	shovelerTilemapAddTileset(borderTilemap, borderTileset);

	ShovelerMaterial *tileSpriteMaterial = shovelerMaterialTileSpriteCreate(game->shaderCache, /* screenspace */ false);
	ShovelerSprite *tileSprite = shovelerSpriteTileCreate(tileSpriteMaterial, tileset, /* tilesetRow */ 0, /* tilesetColumn */ 1);
	tileSprite->position = shovelerVector2(-1.5f, -1.5f);
	tileSprite->size = shovelerVector2(5.0f, 5.0f);
	shovelerCanvasAddSprite(canvas, /* layerId */ 1, tileSprite);

	characterSprite = shovelerSpriteTileCreate(tileSpriteMaterial, animationTileset, /* tilesetRow */ 0, /* tilesetColumn */ 0);
	characterSprite->position = shovelerVector2(0.0f, 0.0f);
	characterSprite->size = shovelerVector2(1.0f, 1.0f);
	shovelerCanvasAddSprite(canvas, /* layerId */ 1, characterSprite);

	animation = shovelerTileSpriteAnimationCreate(characterSprite, shovelerVector2(0.0f, 0.0f), 0.1f);
	animation->moveAmountThreshold = 0.25f;

	ShovelerMaterial *canvasMaterial = shovelerMaterialCanvasCreate(game->shaderCache, /* screenspace */ false);
	shovelerMaterialCanvasSetActive(canvasMaterial, canvas);
	shovelerMaterialCanvasSetActiveRegion(canvasMaterial, shovelerVector2(0.0f, 0.0f), shovelerVector2(10.0f, 10.0f));

	ShovelerDrawable *quad = shovelerDrawableQuadCreate();
	ShovelerModel *model = shovelerModelCreate(quad, canvasMaterial);
	model->scale = shovelerVector3(5.0, 5.0, 1.0);
	model->emitter = true;
	shovelerModelUpdateTransformation(model);
	shovelerSceneAddModel(game->scene, model);

	shovelerOpenGLCheckSuccess();

	while(shovelerGameIsRunning(game)) {
		shovelerGameRenderFrame(game);
	}
	shovelerLogInfo("Exiting main loop, goodbye.");

	shovelerDrawableFree(quad);
	shovelerMaterialFree(canvasMaterial);
	shovelerMaterialFree(tilemapMaterial);
	shovelerMaterialFree(tileSpriteMaterial);
	shovelerCanvasFree(canvas);
	shovelerSpriteFree(tilemapSprite);
	shovelerSpriteFree(borderTilemapSprite);
	shovelerSpriteFree(characterSprite);
	shovelerSpriteFree(tileSprite);
	shovelerTilemapFree(tilemap);
	shovelerTextureFree(tilesTexture);
	shovelerTilemapFree(borderTilemap);
	shovelerTextureFree(borderTilesTexture);
	shovelerTilesetFree(animationTileset);
	shovelerTilesetFree(tileset);
	shovelerTilesetFree(tileset2);
	shovelerTilesetFree(borderTileset);
	shovelerGameFree(game);
	shovelerGlobalUninit();
	shovelerLogTerminate();

	return EXIT_SUCCESS;
}

static void updateGame(ShovelerGame *game, double dt)
{
	ShovelerController *controller = shovelerCameraPerspectiveGetController(game->camera);

	shovelerCameraUpdateView(game->camera);

	float characterWorldX = characterSprite->position.values[0];
	float characterWorldY = characterSprite->position.values[1];

	float moveAmountX = controller->frame.position.values[0] - characterWorldX;
	float moveAmountY = controller->frame.position.values[1] - characterWorldY;
	shovelerTileSpriteAnimationUpdate(animation, shovelerVector2(moveAmountX, moveAmountY));

	characterSprite->position.values[0] = controller->frame.position.values[0];
	characterSprite->position.values[1] = controller->frame.position.values[1];
}
