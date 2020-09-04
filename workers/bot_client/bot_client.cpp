#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <thread>
#include <unordered_set>

#include <improbable/standard_library.h>
#include <improbable/view.h>
#include <improbable/worker.h>
#include <shoveler.h>

#include "connect.h"

extern "C" {
#include <glib.h>

#include <shoveler/image/png.h>
#include <shoveler/executor.h>
#include <shoveler/file.h>
#include <shoveler/image.h>
#include <shoveler/log.h>
}

using Coordinates = improbable::Coordinates;
using improbable::EntityAcl;
using improbable::Interest;
using improbable::Metadata;
using improbable::Persistence;
using ImprobablePosition = improbable::Position;
using ImprobablePositionData = improbable::PositionData;
using shoveler::Bootstrap;
using shoveler::Canvas;
using shoveler::ChunkRegion;
using shoveler::Client;
using shoveler::ClientHeartbeatPing;
using shoveler::ClientHeartbeatPong;
using shoveler::Drawable;
using shoveler::Light;
using shoveler::Material;
using shoveler::Model;
using shoveler::Position;
using shoveler::PositionData;
using shoveler::Resource;
using shoveler::Texture;
using shoveler::Tilemap;
using shoveler::TilemapTiles;
using shoveler::TilemapTilesData;
using shoveler::Tileset;
using shoveler::TileSprite;
using shoveler::TileSpriteAnimation;
using shoveler::Vector3;
using worker::Connection;
using worker::ConnectionParameters;
using worker::EntityId;
using worker::List;
using worker::Option;
using worker::OutgoingCommandRequest;
using worker::RequestId;
using worker::Result;
using worker::View;

using CreateClientEntity = Bootstrap::Commands::CreateClientEntity;

struct TilesData {
	std::string tilesetColumns;
	std::string tilesetRows;
	std::string tilesetIds;
};

struct ClientContext {
	Connection *connection;
	View *view;
	EntityId clientEntityId;
	enum {
		kUp,
		kDown,
		kLeft,
		kRight
	} direction;
	Coordinates lastImprobablePosition;
	int64_t lastHeartbeatPongTime;
	double meanHeartbeatLatencyMs;
	double meanTimeSinceLastHeartbeatPongMs;
};

static void onLogMessage(const worker::LogData& logData);
static void move(ClientContext *context, const PositionData& positionData, int tickPeriodMs);
static bool validatePosition(ClientContext *context, const Vector3& coordinates);
static bool validatePoint(ClientContext *context, const Vector3& coordinates);
static void clientPingTick(void *clientContextPointer);
static void clientDirectionChange(void *clientContextPointer);
static void clientStatus(void *clientContextPointer);
static ShovelerVector2 tileToWorld(int chunkX, int chunkZ, int tileX, int tileZ);
static void worldToTile(double x, double z, int& chunkX, int& chunkZ, int& tileX, int& tileZ);
static EntityId getChunkBackgroundEntityId(int chunkX, int chunkZ);
static Option<TilesData> getChunkBackgroundTiles(worker::Connection &connection, worker::View &view, EntityId chunkBackgroundEntityId);

static const auto connectionRetries = 3;
static const auto connectionRetryDelayMs = 1500;
static const long long int bootstrapEntityId = 1;
static const int64_t clientPingTimeoutMs = 999;
static const int64_t clientDirectionChangeTimeoutMs = 250;
static const int64_t clientStatusTimeoutMs = 2449;
static const float velocity = 1.5;
static const int directionChangeChancePercent = 10;
static const int tickRateHz = 30;
static const int halfMapWidth = 100;
static const int halfMapHeight = 100;
static const int chunkSize = 10;
static const EntityId firstChunkEntityId = 12;
static const int numChunkColumns = 2 * halfMapWidth / chunkSize;
static const int numChunkRows = 2 * halfMapHeight / chunkSize;
static const double characterSize = 0.9;
static const float improbablePositionUpdateDistance = 1.0f;
static const double meanHeartbeatMovingExponentialFactor = 0.5f;
static const double meanTimeSinceLastHeartbeatPongExponentialFactor = 0.05f;

int main(int argc, char **argv) {
	srand(g_get_monotonic_time() % INT64_MAX);

	shovelerLogInit("shoveler-spatialos/", SHOVELER_LOG_LEVEL_INFO_UP, stdout);

	if(argc != 1 && argc != 2 && argc != 4) {
		shovelerLogError("Usage:\n\t%s\n\t%s <launcher link>\n\t%s <worker ID> <hostname> <port>", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	auto components = worker::Components<
		Bootstrap,
		Canvas,
		Client,
		ClientHeartbeatPing,
		ClientHeartbeatPong,
		Drawable,
		EntityAcl,
		ImprobablePosition,
		Interest,
		Light,
		Metadata,
		Material,
		Model,
		Persistence,
		Position,
		Resource,
		Texture,
		Tilemap,
		TilemapTiles,
		Tileset,
		TileSprite,
		TileSpriteAnimation>{};

	worker::LogsinkParameters logsinkParameters;
	logsinkParameters.Type = worker::LogsinkType::kCallback;
	logsinkParameters.Callback = onLogMessage;
	logsinkParameters.FilterParameters.Categories = worker::LogCategory::kNetworkStatus | worker::LogCategory::kLogin;
	logsinkParameters.FilterParameters.Level = worker::LogLevel::kInfo;

	worker::ConnectionParameters parameters;
	parameters.WorkerType = "ShovelerBotClient";
	parameters.Network.ConnectionType = worker::NetworkConnectionType::kModularKcp;
	parameters.Network.ModularKcp.SecurityType = worker::NetworkSecurityType::kDtls;
	parameters.Logsinks = {logsinkParameters};
	parameters.EnableLoggingAtStartup = true;

	worker::Option<Connection> connectionOption;

	for(int i = 0; i < connectionRetries; i++) {
		connectionOption = connect(argc, argv, parameters, components);

		if(connectionOption && connectionOption->IsConnected()) {
			shovelerLogInfo("Connected to SpatialOS deployment!");
			break;
		}

		if(i != connectionRetries - 1) {
			shovelerLogWarning("Connection to SpatialOS failed, will retry in %dms.", connectionRetryDelayMs);
			std::this_thread::sleep_for(std::chrono::milliseconds{connectionRetryDelayMs});
		}
	}

	if(!connectionOption || !connectionOption->IsConnected()) {
		shovelerLogError("Failed to connect to SpatialOS deployment after %d attempts, aborting.", connectionRetries);
		return EXIT_FAILURE;
	}
	Connection& connection = *connectionOption;

	ShovelerExecutor *executor = shovelerExecutorCreateDirect();

	worker::View view{components};
	worker::Dispatcher& dispatcher = view;

	bool disconnected = false;
	bool requestsSent = false;
	ClientContext context;
	context.connection = &connection;
	context.view = &view;
	context.clientEntityId = 0;
	context.direction = ClientContext::kUp;
	context.lastImprobablePosition = Coordinates{0.0, 0.0, 0.0};
	context.clientEntityId = 0;
	context.lastHeartbeatPongTime = g_get_monotonic_time();
	context.meanHeartbeatLatencyMs = 0.0;
	context.meanTimeSinceLastHeartbeatPongMs = 0.5 * (double) clientPingTimeoutMs;

	ShovelerExecutorCallback *clientPingTickCallback = NULL;
	ShovelerExecutorCallback *clientDirectionChangeCallback = shovelerExecutorSchedulePeriodic(executor, 0, clientDirectionChangeTimeoutMs, clientDirectionChange, &context);
	ShovelerExecutorCallback *clientStatusCallback = shovelerExecutorSchedulePeriodic(executor, 0, clientStatusTimeoutMs, clientStatus, &context);

	worker::Option<ChunkRegion> startingChunkRegion;
	auto minXFlag = connection.GetWorkerFlag("starting_chunk_min_x");
	auto minZFlag = connection.GetWorkerFlag("starting_chunk_min_z");
	auto sizeXFlag = connection.GetWorkerFlag("starting_chunk_size_x");
	auto sizeZFlag = connection.GetWorkerFlag("starting_chunk_size_z");
	if(minXFlag && minZFlag && sizeXFlag && sizeZFlag) {
		int minX = std::atoi(minXFlag->c_str());
		int minZ = std::atoi(minXFlag->c_str());
		int sizeX = std::atoi(sizeXFlag->c_str());
		int sizeZ = std::atoi(sizeZFlag->c_str());
		
		startingChunkRegion = ChunkRegion{};
		startingChunkRegion->set_min_x(minX);
		startingChunkRegion->set_min_z(minZ);
		startingChunkRegion->set_size_x(sizeX);
		startingChunkRegion->set_size_z(sizeZ);
		shovelerLogInfo("Overriding starting chunk region to min (%d, %d) and size (%d, %d).", minX, minZ, sizeX, sizeZ);
	}

	Result<RequestId<OutgoingCommandRequest<CreateClientEntity>>> createClientEntityRequestId = connection.SendCommandRequest<CreateClientEntity>(bootstrapEntityId, {startingChunkRegion}, {});
	if(!createClientEntityRequestId) {
		shovelerLogError("Failed to send create client entity request: %s", createClientEntityRequestId.GetErrorMessage().c_str());
		return EXIT_FAILURE;
	}
	shovelerLogInfo("Sent create client entity request with id %lld.", createClientEntityRequestId->Id);

	dispatcher.OnDisconnect([&](const worker::DisconnectOp& op) {
		shovelerLogError("Disconnected from SpatialOS: %s", op.Reason.c_str());
		disconnected = true;
	});

	dispatcher.OnMetrics([&](const worker::MetricsOp& op) {
		auto metrics = op.Metrics;
		connection.SendMetrics(metrics);
	});

	dispatcher.OnAddEntity([&](const worker::AddEntityOp& op) {
		shovelerLogTrace("Adding entity %lld.", op.EntityId);
	});

	dispatcher.OnRemoveEntity([&](const worker::RemoveEntityOp& op) {
		shovelerLogTrace("Removing entity %lld.", op.EntityId);
	});

	dispatcher.OnAddComponent<Client>([&](const worker::AddComponentOp<Client>& op) {
		shovelerLogTrace("Adding client to entity %lld.", op.EntityId);
	});

	dispatcher.OnAuthorityChange<Client>([&](const worker::AuthorityChangeOp& op) {
		shovelerLogInfo("Changing client authority for entity %lld to %d.", op.EntityId, op.Authority);
		if(op.Authority == worker::Authority::kAuthoritative) {
			clientPingTickCallback = shovelerExecutorSchedulePeriodic(executor, 0, clientPingTimeoutMs, clientPingTick, &context);
			context.clientEntityId = op.EntityId;
		} else if(op.Authority == worker::Authority::kNotAuthoritative) {
			shovelerExecutorRemoveCallback(executor, clientPingTickCallback);
			context.clientEntityId = 0;
			shovelerLogError("Lost authority over the client entity %lld.", op.EntityId);
			disconnected = true;
		}
	});

	dispatcher.OnRemoveComponent<Client>([&](const worker::RemoveComponentOp& op) {
		shovelerLogTrace("Removing client from entity %lld.", op.EntityId);
	});

	dispatcher.OnComponentUpdate<ClientHeartbeatPong>([&](const worker::ComponentUpdateOp<ClientHeartbeatPong>& op) {
		if(op.EntityId != context.clientEntityId) {
			shovelerLogWarning("Received ClientHeartbeatPong update for entity %lld that isn't the client entity %lld, which points to a broken interest setup", op.EntityId, context.clientEntityId);
		}

		if(!op.Update.last_updated_time()) {
			return;
		}

		context.lastHeartbeatPongTime = g_get_monotonic_time();
		context.meanHeartbeatLatencyMs *= (1.0 - meanHeartbeatMovingExponentialFactor);
		context.meanHeartbeatLatencyMs += meanHeartbeatMovingExponentialFactor * 0.001 * (double) (context.lastHeartbeatPongTime - *op.Update.last_updated_time());
	});

	dispatcher.OnCommandResponse<CreateClientEntity>([&](const worker::CommandResponseOp<CreateClientEntity>& op) {
		shovelerLogInfo("Received create client entity command response %lld with status code %d.", op.RequestId.Id, op.StatusCode);
	});

	int tickPeriodMs = (int) (1000.0 / (double) tickRateHz);
	while(!disconnected) {
		gint64 tickStartTime = g_get_monotonic_time();

		dispatcher.Process(connection.GetOpList(0));
		shovelerExecutorUpdateNow(executor);

		if(context.clientEntityId != 0) {
			const auto& clientEntity = view.Entities.find(context.clientEntityId);
			if(clientEntity != view.Entities.end()) {
				const auto& clientComponentAuthority = view.ComponentAuthority.find(context.clientEntityId);
				if(clientComponentAuthority != view.ComponentAuthority.end()) {
					const auto& clientPositionAuthority = clientComponentAuthority->second.find(Position::ComponentId);
					if(clientPositionAuthority != clientComponentAuthority->second.end()) {
						if(clientPositionAuthority->second == worker::Authority::kAuthoritative) {
							const Option<PositionData&>& position = clientEntity->second.Get<Position>();
							if(position) {
								move(&context, *position, tickPeriodMs);
							}
						}
					}
				}
			}
		}

		gint64 tickEndTime = g_get_monotonic_time();
		gint64 remainingNs = 1000 * 1000 * tickPeriodMs - (tickEndTime - tickStartTime);
		if(remainingNs > 0) {
			std::this_thread::sleep_for(std::chrono::nanoseconds(remainingNs));
		}
	}

	shovelerExecutorRemoveCallback(executor, clientPingTickCallback);
	shovelerExecutorRemoveCallback(executor, clientDirectionChangeCallback);
	shovelerExecutorRemoveCallback(executor, clientStatusCallback);
	shovelerExecutorFree(executor);
}

static void onLogMessage(const worker::LogData& logData)
{
	switch(logData.Level) {
		case worker::LogLevel::kDebug:
			shovelerLogTrace("[Worker SDK] %s", logData.Content.c_str());
			break;
		case worker::LogLevel::kInfo:
			shovelerLogInfo("[Worker SDK] %s", logData.Content.c_str());
			break;
		case worker::LogLevel::kWarn:
			shovelerLogWarning("[Worker SDK] %s", logData.Content.c_str());
			break;
		case worker::LogLevel::kError:
			shovelerLogError("[Worker SDK] %s", logData.Content.c_str());
			break;
		case worker::LogLevel::kFatal:
			shovelerLogError("[Worker SDK] [FATAL] %s", logData.Content.c_str());
			break;
	}
}

static void move(ClientContext *context, const PositionData& positionData, int tickPeriodMs) {
	Vector3 coordinates = positionData.coordinates();

	float s = 0.001f * tickPeriodMs * velocity;

	switch(context->direction) {
		case ClientContext::kUp:
			coordinates.y() += s;
			break;
		case ClientContext::kDown:
			coordinates.y() -= s;
			break;
		case ClientContext::kLeft:
			coordinates.x() -= s;
			break;
		case ClientContext::kRight:
			coordinates.x() += s;
			break;
	}

	if(!validatePosition(context, coordinates)) {
		clientDirectionChange(context);
		return;
	}

	Position::Update positionUpdate;
	positionUpdate.set_coordinates(coordinates);
	context->connection->SendComponentUpdate<Position>(context->clientEntityId, positionUpdate);

	shovelerLogTrace("Sent position update for client entity %lld to (%.2f, %.2f, %.2f).", context->clientEntityId, coordinates.x(), coordinates.y(), coordinates.z());

	Coordinates improbablePosition{coordinates.x(), coordinates.z(), coordinates.y()};
	float dx = improbablePosition.x() - context->lastImprobablePosition.x();
	float dy = improbablePosition.y() - context->lastImprobablePosition.y();
	float dz = improbablePosition.z() - context->lastImprobablePosition.z();
	float difference2 = dx * dx + dy * dy + dz * dz;
	if(difference2 > improbablePositionUpdateDistance) {
		ImprobablePosition::Update improbablePositionUpdate;
		improbablePositionUpdate.set_coords(improbablePosition);
		context->connection->SendComponentUpdate<ImprobablePosition>(context->clientEntityId, improbablePositionUpdate);

		context->lastImprobablePosition = improbablePosition;
	}
}

static bool validatePosition(ClientContext *context, const Vector3& coordinates) {
	Vector3 topRight = coordinates;
	topRight.x() += 0.5 * characterSize;
	topRight.y() += 0.5 * characterSize;
	if(!validatePoint(context, topRight)) {
		return false;
	}

	Vector3 topLeft = coordinates;
	topLeft.x() -= 0.5 * characterSize;
	topLeft.y() += 0.5 * characterSize;
	if(!validatePoint(context, topLeft)) {
		return false;
	}

	Vector3 bottomRight = coordinates;
	bottomRight.x() += 0.5 * characterSize;
	bottomRight.y() -= 0.5 * characterSize;
	if(!validatePoint(context, bottomRight)) {
		return false;
	}

	Vector3 bottomLeft = coordinates;
	bottomLeft.x() -= 0.5 * characterSize;
	bottomLeft.y() -= 0.5 * characterSize;
	if(!validatePoint(context, bottomLeft)) {
		return false;
	}

	return true;
}

static bool validatePoint(ClientContext *context, const Vector3& coordinates) {
	double x = coordinates.x();
	double z = coordinates.y();
	int chunkX, chunkZ, tileX, tileZ;
	worldToTile(x, z, chunkX, chunkZ, tileX, tileZ);

	if(chunkX < 0 || chunkX >= numChunkColumns || chunkZ < 0 || chunkZ >= numChunkRows || tileX < 0 || tileX >= chunkSize || tileZ < 0 || tileZ >= chunkSize) {
		shovelerLogTrace("Position (%.2f, %.2f, %.2f) validates to false because tile coordinates are invalid.", coordinates.x(), coordinates.y(), coordinates.z());
		return false;
	}

	worker::EntityId chunkBackgroundEntityId = getChunkBackgroundEntityId(chunkX, chunkZ);
	Option<TilesData> tiles = getChunkBackgroundTiles(*context->connection, *context->view, chunkBackgroundEntityId);
	if(!tiles) {
		shovelerLogTrace("Position (%.2f, %.2f, %.2f) validates to false because background tiles are empty.", coordinates.x(), coordinates.y(), coordinates.z());
		return false;
	}

	char tilesetColumn = tiles->tilesetColumns[tileZ * chunkSize + tileX];
	if(tilesetColumn > 2) { // tile isn't grass
		shovelerLogTrace("Position (%.2f, %.2f, %.2f) validates to false because tile isn't grass.", coordinates.x(), coordinates.y(), coordinates.z());
		return false;
	}

	return true;
}

static void clientPingTick(void *clientContextPointer) {
	ClientContext *context = (ClientContext *) clientContextPointer;

	ClientHeartbeatPing::Update update;
	update.set_last_updated_time(g_get_monotonic_time());
	context->connection->SendComponentUpdate<ClientHeartbeatPing>(context->clientEntityId, update);
}

static void clientDirectionChange(void *clientContextPointer) {
	ClientContext *context = (ClientContext *) clientContextPointer;

	if(rand() % 100 >= directionChangeChancePercent) {
		return;
	}

	context->direction = static_cast<decltype(context->direction)>((context->direction + 1 + (rand() % 3)) % 4);
	shovelerLogTrace("Changing direction to %u.", context->direction);
}

static void clientStatus(void *clientContextPointer)
{
	ClientContext *context = (ClientContext *) clientContextPointer;

	context->meanTimeSinceLastHeartbeatPongMs *= (1.0 - meanTimeSinceLastHeartbeatPongExponentialFactor);
	context->meanTimeSinceLastHeartbeatPongMs += meanTimeSinceLastHeartbeatPongExponentialFactor * 0.001 * (double) (g_get_monotonic_time() - context->lastHeartbeatPongTime);

	shovelerLogInfo(
		"Entities: %zu\t\tLatency: %.0fms\t\tDesync: %.0fms",
		context->view->Entities.size(),
		context->meanHeartbeatLatencyMs,
		fabs(context->meanTimeSinceLastHeartbeatPongMs - 0.5 * (double) clientPingTimeoutMs));
}

static ShovelerVector2 tileToWorld(int chunkX, int chunkZ, int tileX, int tileZ) {
	return shovelerVector2(-halfMapWidth + chunkX * chunkSize + tileX, -halfMapHeight + chunkZ * chunkSize + tileZ);
}

static void worldToTile(double x, double z, int& chunkX, int& chunkZ, int& tileX, int& tileZ) {
	double diffX = x + halfMapWidth;
	double diffZ = z + halfMapHeight;

	chunkX = (int) floor(diffX / chunkSize);
	chunkZ = (int) floor(diffZ / chunkSize);

	tileX = (int) floor(diffX - chunkX * chunkSize);
	tileZ = (int) floor(diffZ - chunkZ * chunkSize);
}

static EntityId getChunkBackgroundEntityId(int chunkX, int chunkZ) {
	if(chunkX < 0 || chunkX >= numChunkColumns || chunkZ < 0 || chunkZ >= numChunkRows) {
		shovelerLogWarning("Cannot resolve chunk background entity id for out of range chunk at (%d, %d).", chunkX, chunkZ);
		return 0;
	}

	return firstChunkEntityId + 3 * chunkX * numChunkColumns + 3 * chunkZ;
}

static Option<TilesData> getChunkBackgroundTiles(worker::Connection &connection, worker::View &view, EntityId chunkBackgroundEntityId)
{
	worker::Map<worker::EntityId, worker::Entity>::const_iterator chunkBackgroundEntityQuery = view.Entities.find(chunkBackgroundEntityId);
	if(chunkBackgroundEntityQuery == view.Entities.end()) {
		shovelerLogWarning("Chunk background entity %lld is not in view.", chunkBackgroundEntityId);
		return {};
	}

	const worker::Entity& chunkBackgroundEntity = chunkBackgroundEntityQuery->second;
	worker::Option<const TilemapTilesData&> tilemapTilesComponent = chunkBackgroundEntity.Get<TilemapTiles>();
	if(!tilemapTilesComponent) {
		shovelerLogWarning("Supposed chunk background entity %lld doesn't have a tilemap tiles component.", chunkBackgroundEntityId);
		return {};
	}

	return {{*tilemapTilesComponent->tileset_columns(), *tilemapTilesComponent->tileset_rows(), *tilemapTilesComponent->tileset_ids()}};
}
