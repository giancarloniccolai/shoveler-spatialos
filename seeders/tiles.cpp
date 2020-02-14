#include <cstdlib>

#include <improbable/standard_library.h>
#include <improbable/worker.h>
#include <shoveler.h>

extern "C" {
#include <glib.h>

#include <shoveler/image/png.h>
#include <shoveler/resources/image_png.h>
#include <shoveler/constants.h>
#include <shoveler/file.h>
#include <shoveler/log.h>
}

#include "tiles/map.h"
#include "tiles/tileset.h"

using improbable::ComponentInterest;
using improbable::EntityAcl;
using improbable::Interest;
using improbable::InterestData;
using improbable::Metadata;
using improbable::Persistence;
using improbable::Position;
using improbable::WorkerAttributeSet;
using improbable::WorkerRequirementSet;
using shoveler::Bootstrap;
using shoveler::Canvas;
using shoveler::Chunk;
using shoveler::ChunkLayer;
using shoveler::ChunkLayerType;
using shoveler::Client;
using shoveler::CoordinateMapping;
using shoveler::Drawable;
using shoveler::DrawableType;
using shoveler::Material;
using shoveler::MaterialType;
using shoveler::Model;
using shoveler::PolygonMode;
using shoveler::Resource;
using shoveler::Sampler;
using shoveler::Texture;
using shoveler::Tilemap;
using shoveler::TilemapColliders;
using shoveler::TilemapTiles;
using shoveler::Tileset;
using shoveler::TileSprite;
using worker::ComponentRegistry;
using worker::Components;
using worker::Entity;
using worker::EntityId;
using worker::List;
using worker::Map;
using worker::Option;
using worker::Result;
using worker::SnapshotOutputStream;
using worker::StreamErrorCode;

using Query = ComponentInterest::Query;
using QueryConstraint = ComponentInterest::QueryConstraint;

static GString *getImageData(ShovelerImage *image);

static const unsigned int chunkSize = 10;

int main(int argc, char **argv) {
	shovelerLogInit("ShovelCrest/workers/cmake/", SHOVELER_LOG_LEVEL_INFO_UP, stdout);

	if (argc != 10) {
		shovelerLogError("Usage:\n\t%s <tileset png> <tileset columns> <tileset rows> <character png> <character2 png> <character3 png> <character4 png> <character shift amount> <seed snapshot file>", argv[0]);
		return EXIT_FAILURE;
	}

	std::string tilesetPngFilename(argv[1]);
	int tilesetPngColumns = atoi(argv[2]);
	int tilesetPngRows = atoi(argv[3]);
	std::string characterPngFilename(argv[4]);
	std::string character2PngFilename(argv[5]);
	std::string character3PngFilename(argv[6]);
	std::string character4PngFilename(argv[7]);
	int characterShiftAmount = atoi(argv[8]);
	std::string snapshotFilename(argv[9]);

	const ComponentRegistry& components = Components<
		Bootstrap,
		Canvas,
		Chunk,
		ChunkLayer,
		Client,
		Drawable,
		EntityAcl,
		Interest,
		Metadata,
		Material,
		Model,
		Persistence,
		Position,
		Resource,
		Sampler,
		Texture,
		Tilemap,
		TilemapColliders,
		TilemapTiles,
		Tileset,
		TileSprite>{};

	WorkerAttributeSet clientAttributeSet({"client"});
	WorkerAttributeSet serverAttributeSet({"server"});
	WorkerRequirementSet clientRequirementSet({clientAttributeSet});
	WorkerRequirementSet serverRequirementSet({serverAttributeSet});
	WorkerRequirementSet clientOrServerRequirementSet({clientAttributeSet, serverAttributeSet});

	worker::Map<std::uint32_t, WorkerRequirementSet> resourceToServerAclMap;
	resourceToServerAclMap.insert({{Resource::ComponentId, serverRequirementSet}});

	std::unordered_map<EntityId, Entity> entities;

	Entity bootstrapEntity;
	bootstrapEntity.Add<Metadata>({"bootstrap"});
	bootstrapEntity.Add<Persistence>({});
	bootstrapEntity.Add<Position>({{-100, -100, -100}});
	bootstrapEntity.Add<Bootstrap>({});
	Map<std::uint32_t, WorkerRequirementSet> bootstrapComponentAclMap;
	bootstrapComponentAclMap.insert({{Bootstrap::ComponentId, serverRequirementSet}});
	bootstrapEntity.Add<EntityAcl>({clientOrServerRequirementSet, bootstrapComponentAclMap});
	Query query;
	QueryConstraint queryConstraint;
	queryConstraint.set_component_constraint(Client::ComponentId);
	query.set_constraint(queryConstraint);
	query.set_full_snapshot_result({true});
	ComponentInterest componentInterest;
	componentInterest.set_queries({query});
	InterestData interestData;
	interestData.component_interest()[Bootstrap::ComponentId] = componentInterest;
	bootstrapEntity.Add<Interest>(interestData);
	entities[1] = bootstrapEntity;

	Entity quadDrawableEntity;
	quadDrawableEntity.Add<Metadata>({"drawable"});
	quadDrawableEntity.Add<Persistence>({});
	quadDrawableEntity.Add<Position>({{-100, -100, -100}});
	quadDrawableEntity.Add<Drawable>({DrawableType::QUAD, {}, {}});
	quadDrawableEntity.Add<EntityAcl>({clientOrServerRequirementSet, {}});
	entities[2] = quadDrawableEntity;

	ShovelerImage *tilesetImage;
	int tilesetColumns;
	int tilesetRows;
	createTileset(&tilesetImage, &tilesetColumns, &tilesetRows);
	GString *imageData = getImageData(tilesetImage);
	shovelerImageFree(tilesetImage);
	Entity tilesetEntity;
	tilesetEntity.Add<Metadata>({"tileset"});
	tilesetEntity.Add<Persistence>({});
	tilesetEntity.Add<Position>({{-100, -100, -100}});
	tilesetEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{imageData->str, imageData->len}});
	tilesetEntity.Add<Sampler>({true, false, true});
	tilesetEntity.Add<Texture>({3});
	tilesetEntity.Add<Tileset>({3, tilesetColumns, tilesetRows, 1});
	tilesetEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(imageData, true);
	entities[3] = tilesetEntity;

	ShovelerImage *tilesetPngImage = shovelerImagePngReadFile(tilesetPngFilename.c_str());
	GString *tilesetPngData = getImageData(tilesetPngImage);
	shovelerImageFree(tilesetPngImage);
	Entity tilesetPngEntity;
	tilesetPngEntity.Add<Metadata>({"tileset"});
	tilesetPngEntity.Add<Persistence>({});
	tilesetPngEntity.Add<Position>({{-100, -100, -100}});
	tilesetPngEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{tilesetPngData->str, tilesetPngData->len}});
	tilesetPngEntity.Add<Sampler>({true, false, true});
	tilesetPngEntity.Add<Texture>({4});
	tilesetPngEntity.Add<Tileset>({4, tilesetPngColumns, tilesetPngRows, 1});
	tilesetPngEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(tilesetPngData, true);
	entities[4] = tilesetPngEntity;

	ShovelerImage *characterPngImage = shovelerImagePngReadFile(characterPngFilename.c_str());
	ShovelerImage *characterAnimationTilesetImage = shovelerImageCreateAnimationTileset(characterPngImage, characterShiftAmount);
	GString *characterAnimationTilesetPngData = getImageData(characterAnimationTilesetImage);
	shovelerImageFree(characterPngImage);
	shovelerImageFree(characterAnimationTilesetImage);
	Entity characterAnimationTilesetEntity;
	characterAnimationTilesetEntity.Add<Metadata>({"tileset"});
	characterAnimationTilesetEntity.Add<Persistence>({});
	characterAnimationTilesetEntity.Add<Position>({{-100, -100, -100}});
	characterAnimationTilesetEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{characterAnimationTilesetPngData->str, characterAnimationTilesetPngData->len}});
	characterAnimationTilesetEntity.Add<Sampler>({true, false, true});
	characterAnimationTilesetEntity.Add<Texture>({5});
	characterAnimationTilesetEntity.Add<Tileset>({5, 4, 3, 1});
	characterAnimationTilesetEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(characterAnimationTilesetPngData, true);
	entities[5] = characterAnimationTilesetEntity;

	ShovelerImage *character2PngImage = shovelerImagePngReadFile(character2PngFilename.c_str());
	ShovelerImage *character2AnimationTilesetImage = shovelerImageCreateAnimationTileset(character2PngImage, characterShiftAmount);
	GString *character2AnimationTilesetPngData = getImageData(character2AnimationTilesetImage);
	shovelerImageFree(character2PngImage);
	shovelerImageFree(character2AnimationTilesetImage);
	Entity character2AnimationTilesetEntity;
	character2AnimationTilesetEntity.Add<Metadata>({"tileset"});
	character2AnimationTilesetEntity.Add<Persistence>({});
	character2AnimationTilesetEntity.Add<Position>({{-100, -100, -100}});
	character2AnimationTilesetEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{character2AnimationTilesetPngData->str, character2AnimationTilesetPngData->len}});
	character2AnimationTilesetEntity.Add<Sampler>({true, false, true});
	character2AnimationTilesetEntity.Add<Texture>({6});
	character2AnimationTilesetEntity.Add<Tileset>({6, 4, 3, 1});
	character2AnimationTilesetEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(character2AnimationTilesetPngData, true);
	entities[6] = character2AnimationTilesetEntity;

	ShovelerImage *character3PngImage = shovelerImagePngReadFile(character3PngFilename.c_str());
	ShovelerImage *character3AnimationTilesetImage = shovelerImageCreateAnimationTileset(character3PngImage, characterShiftAmount);
	GString *character3AnimationTilesetPngData = getImageData(character3AnimationTilesetImage);
	shovelerImageFree(character3PngImage);
	shovelerImageFree(character3AnimationTilesetImage);
	Entity character3AnimationTilesetEntity;
	character3AnimationTilesetEntity.Add<Metadata>({"tileset"});
	character3AnimationTilesetEntity.Add<Persistence>({});
	character3AnimationTilesetEntity.Add<Position>({{-100, -100, -100}});
	character3AnimationTilesetEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{character3AnimationTilesetPngData->str, character3AnimationTilesetPngData->len}});
	character3AnimationTilesetEntity.Add<Sampler>({true, false, true});
	character3AnimationTilesetEntity.Add<Texture>({7});
	character3AnimationTilesetEntity.Add<Tileset>({7, 4, 3, 1});
	character3AnimationTilesetEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(character3AnimationTilesetPngData, true);
	entities[7] = character3AnimationTilesetEntity;

	ShovelerImage *character4PngImage = shovelerImagePngReadFile(character4PngFilename.c_str());
	ShovelerImage *character4AnimationTilesetImage = shovelerImageCreateAnimationTileset(character4PngImage, characterShiftAmount);
	GString *character4AnimationTilesetPngData = getImageData(character4AnimationTilesetImage);
	shovelerImageFree(character4PngImage);
	shovelerImageFree(character4AnimationTilesetImage);
	Entity character4AnimationTilesetEntity;
	character4AnimationTilesetEntity.Add<Metadata>({"tileset"});
	character4AnimationTilesetEntity.Add<Persistence>({});
	character4AnimationTilesetEntity.Add<Position>({{-100, -100, -100}});
	character4AnimationTilesetEntity.Add<Resource>({shovelerResourcesImagePngTypeId, std::string{character4AnimationTilesetPngData->str, character4AnimationTilesetPngData->len}});
	character4AnimationTilesetEntity.Add<Sampler>({true, false, true});
	character4AnimationTilesetEntity.Add<Texture>({8});
	character4AnimationTilesetEntity.Add<Tileset>({8, 4, 3, 1});
	character4AnimationTilesetEntity.Add<EntityAcl>({clientOrServerRequirementSet, resourceToServerAclMap});
	g_string_free(character4AnimationTilesetPngData, true);
	entities[8] = character4AnimationTilesetEntity;

	EntityId canvasEntityId = 9;
	Entity canvasEntity;
	canvasEntity.Add<Metadata>({"canvas"});
	canvasEntity.Add<Material>({MaterialType::TILE_SPRITE, {}, {}, {}, {}, {}, {}, {}, {}});
	canvasEntity.Add<Persistence>({});
	canvasEntity.Add<Position>({{-100, -100, -100}});
	canvasEntity.Add<Canvas>({{}});
	canvasEntity.Add<ChunkLayer>({ChunkLayerType::CANVAS, {canvasEntityId}, {}});
	worker::Map<std::uint32_t, WorkerRequirementSet> canvasComponentAclMap;
	canvasComponentAclMap.insert({{Canvas::ComponentId, serverRequirementSet}});
	canvasEntity.Add<EntityAcl>({clientOrServerRequirementSet, canvasComponentAclMap});
	entities[canvasEntityId] = canvasEntity;

	EntityId nextEntityId = 10;

	List<ChunkData> chunks = generateMapChunks(10);
	for(List<ChunkData>::const_iterator iter = chunks.begin(); iter != chunks.end(); ++iter) {
		ChunkData chunk = *iter;

		Entity chunkBackgroundEntity;
		chunkBackgroundEntity.Add<Metadata>({"chunk_background"});
		chunkBackgroundEntity.Add<Persistence>({});
		chunkBackgroundEntity.Add<Position>({chunk.position});
		chunkBackgroundEntity.Add<TilemapColliders>({chunkSize, chunkSize, chunk.backgroundTiles.tilesetColliders});
		chunkBackgroundEntity.Add<TilemapTiles>({{}, {chunkSize}, {chunkSize}, {chunk.backgroundTiles.tilesetColumns}, {chunk.backgroundTiles.tilesetRows}, {chunk.backgroundTiles.tilesetIds}});
		chunkBackgroundEntity.Add<Tilemap>({nextEntityId, nextEntityId, {3, 4}});
		chunkBackgroundEntity.Add<ChunkLayer>({ChunkLayerType::TILEMAP, {}, {nextEntityId}});
		worker::Map<std::uint32_t, WorkerRequirementSet> chunkBackgroundComponentAclMap;
		chunkBackgroundComponentAclMap.insert({{TilemapTiles::ComponentId, serverRequirementSet}});
		chunkBackgroundEntity.Add<EntityAcl>({clientOrServerRequirementSet, chunkBackgroundComponentAclMap});
		EntityId backgroundEntityId = nextEntityId;
		entities[backgroundEntityId] = chunkBackgroundEntity;
		nextEntityId++;

		Entity chunkForegroundEntity;
		chunkForegroundEntity.Add<Metadata>({"chunk_foreground"});
		chunkForegroundEntity.Add<Persistence>({});
		chunkForegroundEntity.Add<Position>({chunk.position});
		chunkForegroundEntity.Add<TilemapColliders>({chunkSize, chunkSize, chunk.foregroundTiles.tilesetColliders});
		chunkForegroundEntity.Add<TilemapTiles>({{}, {chunkSize}, {chunkSize}, {chunk.foregroundTiles.tilesetColumns}, {chunk.foregroundTiles.tilesetRows}, {chunk.foregroundTiles.tilesetIds}});
		chunkForegroundEntity.Add<Tilemap>({nextEntityId, nextEntityId, {3, 4}});
		chunkForegroundEntity.Add<ChunkLayer>({ChunkLayerType::TILEMAP, {}, {nextEntityId}});
		chunkForegroundEntity.Add<EntityAcl>({clientOrServerRequirementSet, {}});
		EntityId foregroundEntityId = nextEntityId;
		entities[foregroundEntityId] = chunkForegroundEntity;
		nextEntityId++;

		Entity chunkEntity;
		chunkEntity.Add<Metadata>({"chunk"});
		chunkEntity.Add<Persistence>({});
		chunkEntity.Add<Position>({chunk.position});
		chunkEntity.Add<Chunk>({nextEntityId, CoordinateMapping::POSITIVE_X, CoordinateMapping::POSITIVE_Y, {(float) chunkSize, (float) chunkSize}, {backgroundEntityId, canvasEntityId, foregroundEntityId}});
		chunkEntity.Add<Material>({MaterialType::CHUNK, {}, {}, {}, {}, {nextEntityId}, {}, {}, {}});
		chunkEntity.Add<Model>({nextEntityId, 2, nextEntityId, {0.0, 0.0, 0.0}, {chunkSize / 2, chunkSize / 2, 1.0}, true, true, false, PolygonMode::FILL});
		chunkEntity.Add<EntityAcl>({clientOrServerRequirementSet, {}});
		entities[nextEntityId] = chunkEntity;
		nextEntityId++;
	}

	Result<SnapshotOutputStream, StreamErrorCode> outputStream = SnapshotOutputStream::Create(components, snapshotFilename);
	if(!outputStream) {
		shovelerLogError("Failed to open snapshot stream: %s", outputStream.GetErrorMessage().c_str());
		return EXIT_FAILURE;
	}

	for(std::unordered_map<EntityId, Entity>::const_iterator iter = entities.begin(); iter != entities.end(); ++iter) {
		Result<worker::None, StreamErrorCode> entityWritten = outputStream->WriteEntity(iter->first, iter->second);
		if(!entityWritten) {
			shovelerLogError("Failed to write entity %lld to snapshot: %s", iter->first, entityWritten.GetErrorMessage().c_str());
			return EXIT_FAILURE;
		}
	}

	shovelerLogInfo("Successfully wrote snapshot with %zu entities.", entities.size());
	return EXIT_SUCCESS;
}

static GString *getImageData(ShovelerImage *image)
{
	const char *tempImageFilename = "temp.png";
	shovelerImagePngWriteFile(image, tempImageFilename);

	unsigned char *contents;
	size_t contentsSize;
	shovelerFileRead(tempImageFilename, &contents, &contentsSize);

	GString *data = g_string_new("");
	g_string_append_len(data, (gchar *) contents, contentsSize);
	free(contents);

	return data;
}
