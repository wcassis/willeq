# WillEQ Docker Build

Docker containers for building and running WillEQ with X11 forwarding or VNC support.

## Quick Start

### Build the image

```bash
# Build runtime image (X11 mode)
docker build -t willeq -f docker/Dockerfile .

# Build VNC-enabled image
docker build -t willeq-vnc -f docker/Dockerfile --target vnc .
```

### Run with VNC (recommended for remote/headless)

```bash
docker run -d \
  -p 5900:5900 \
  -v /path/to/EverQuest:/eq:ro \
  -v /path/to/config:/config \
  -e VNC=1 \
  willeq-vnc

# Connect with any VNC client to: vnc://localhost:5900
```

### Run with X11 forwarding (Linux host)

```bash
# Allow X11 connections (run once)
xhost +local:docker

# Run with display forwarding
docker run -it \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /path/to/EverQuest:/eq:ro \
  -v /path/to/config:/config \
  willeq
```

### Run headless (no graphics)

```bash
docker run -it \
  -v /path/to/EverQuest:/eq:ro \
  -v /path/to/config:/config \
  willeq willeq -c /config/willeq.json --no-graphics
```

## Using Docker Compose

Docker Compose provides pre-configured services for common use cases:

```bash
# Set paths (or create .env file)
export EQ_CLIENT_PATH=/path/to/EverQuest
export EQ_CONFIG_PATH=/path/to/config

# Run with VNC
docker compose -f docker/docker-compose.yml up willeq-vnc

# Run with X11 forwarding
docker compose -f docker/docker-compose.yml up willeq

# Run headless
docker compose -f docker/docker-compose.yml up willeq-headless

# Build only (outputs to ./dist)
docker compose -f docker/docker-compose.yml run --rm builder
```

### Environment Variables

Create a `.env` file in the project root:

```env
EQ_CLIENT_PATH=/home/user/EverQuest
EQ_CONFIG_PATH=/home/user/eqconfig
RESOLUTION=1024x768x24
```

## Build Stages

The Dockerfile uses multi-stage builds:

| Stage | Description | Use Case |
|-------|-------------|----------|
| `builder` | Full build environment | Building binaries |
| `runtime` | Minimal runtime + X11 | Production with X11 forwarding |
| `vnc` | Runtime + VNC server | Headless servers, remote access |

### Building specific stages

```bash
# Builder only (for extracting binaries)
docker build -t willeq-builder --target builder -f docker/Dockerfile .
docker run --rm -v $(pwd)/dist:/dist willeq-builder \
  sh -c "cp /build/bin/* /dist/"

# Runtime only (smaller image)
docker build -t willeq --target runtime -f docker/Dockerfile .

# VNC-enabled
docker build -t willeq-vnc --target vnc -f docker/Dockerfile .
```

## Configuration

### Config file

Mount your config directory and ensure `willeq.json` exists:

```json
{
    "eq_client_path": "/eq"
}
```

Note: Inside the container, EQ files are mounted at `/eq`, so use that path in the config.

### VNC Options

| Variable | Default | Description |
|----------|---------|-------------|
| `VNC` | `0` | Set to `1` to enable VNC mode |
| `VNC_PORT` | `5900` | VNC server port |
| `DISPLAY` | `:99` | X display number (VNC mode) |
| `RESOLUTION` | `800x600x24` | Virtual display resolution |

### Examples

```bash
# High resolution VNC
docker run -d \
  -p 5900:5900 \
  -e VNC=1 \
  -e RESOLUTION=1920x1080x24 \
  -v /path/to/EverQuest:/eq:ro \
  -v /path/to/config:/config \
  willeq-vnc

# Custom VNC port
docker run -d \
  -p 5999:5999 \
  -e VNC=1 \
  -e VNC_PORT=5999 \
  -v /path/to/EverQuest:/eq:ro \
  -v /path/to/config:/config \
  willeq-vnc
```

## Volume Mounts

| Container Path | Description | Required |
|----------------|-------------|----------|
| `/eq` | EverQuest Titanium client files | Yes (for graphics) |
| `/config` | Configuration files | Yes |
| `/tmp/.X11-unix` | X11 socket (X11 mode only) | X11 mode |

## Troubleshooting

### X11: Cannot open display

```bash
# On host, allow Docker X11 access
xhost +local:docker

# Or use VNC mode instead
```

### VNC: Connection refused

Check the container is running and port is exposed:
```bash
docker ps
docker logs <container_id>
```

### Graphics: Software rendering

The container uses software rendering by default (Irrlicht's software driver). This works without GPU but is slower than hardware acceleration.

### Build fails: Missing dependencies

Ensure you're building from the project root:
```bash
cd /path/to/eqt-irrlicht
docker build -f docker/Dockerfile .
```
