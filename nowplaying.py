import asyncio
import io
import re
import time
import threading
import sys

from quart import Quart, jsonify, send_file, request
from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as MediaManager
from winrt.windows.storage.streams import DataReader, Buffer, InputStreamOptions

import pystray
from PIL import Image, ImageDraw
from PIL import Image as PILImage
from hypercorn.asyncio import serve
from hypercorn.config import Config

# --- Quart app ---
app = Quart(__name__)

# --- Caching ---
CACHE_DURATION = 0.5  
_cached_track_info = None
_cached_album_art = None
_last_update = 0
_last_position_timestamp = 0
_session = None 

# --- Shutdown control ---
shutdown_event = asyncio.Event()

# --- Helpers ---
def clean_text(text):
    if not text:
        return ""
    text = text.replace("–", "-").replace("—", "-")
    text = re.sub(r'\s*-\s*single\s*$', '', text, flags=re.IGNORECASE)
    return text.strip()

def clean_artist(text):
    if not text:
        return ""
    text = text.replace("–", "-").replace("—", "-")
    parts = re.split(r"\s*-\s*", text, maxsplit=1)
    return parts[0].strip()

def format_time(seconds):
    seconds = max(0, int(seconds))
    m, s = divmod(seconds, 60)
    return f"{m}:{s:02}"

async def get_apple_music_session():
    global _session
    try:
        if _session is None:
            sessions = await MediaManager.request_async()
            for s in sessions.get_sessions():
                app_name = s.source_app_user_model_id
                if "Music" in app_name or "Apple" in app_name:
                    _session = s
                    break
        else:
            # test if still valid
            _ = await _session.try_get_media_properties_async()
    except Exception:
        _session = None
    return _session


async def fetch_now_playing():
    session = await get_apple_music_session()
    if not session:
        return None, 0, "Stopped"

    info = await session.try_get_media_properties_async()
    timeline = session.get_timeline_properties()
    controls = session.get_playback_info()

    current_pos = timeline.position.total_seconds() if timeline.position else 0
    end_pos = timeline.end_time.total_seconds() if timeline.end_time else 0

    state = str(controls.playback_status)  # "Playing", "Paused", "Stopped"

    track_info = {
        "title": clean_text(info.title),
        "artist": clean_artist(info.artist),
        "duration": format_time(end_pos),
        "state": state
    }
    return track_info, current_pos, state

async def fetch_album_art():
    session = await get_apple_music_session()
    if not session:
        return None
    info = await session.try_get_media_properties_async()
    if not info.thumbnail:
        return None
    stream_ref = info.thumbnail
    stream = await stream_ref.open_read_async()
    size = stream.size
    buffer = Buffer(size)
    await stream.read_async(buffer, size, InputStreamOptions.NONE)
    data = bytearray(buffer.length)
    DataReader.from_buffer(buffer).read_bytes(data)
    return bytes(data)

async def update_track_cache():
    #Fetch and update track info, reset session if Apple Music was closed.
    global _cached_track_info, _last_update, _last_position_timestamp, _cached_album_art, _session

    try:
        track_info, current_pos, state = await fetch_now_playing()
    except Exception:
        # If session is broken → force reset
        _cached_track_info = None
        _cached_album_art = None
        _session = None
        return

    if not track_info:
        # No valid session or no track
        _cached_track_info = None
        _cached_album_art = None
        _session = None  # force reacquire next time
        return

    # Detect if song changed (title or artist different)
    song_changed = (
        not _cached_track_info
        or _cached_track_info.get("title") != track_info["title"]
        or _cached_track_info.get("artist") != track_info["artist"]
    )

    # Update cached info
    _cached_track_info = {
        **track_info,
        "last_fetched_pos": current_pos,
        "state": state,
    }
    _last_position_timestamp = time.time()
    _last_update = time.time()

    # Refresh album art only when the song changes
    if song_changed:
        try:
            raw_art = await fetch_album_art()
            if raw_art:
                img = PILImage.open(io.BytesIO(raw_art)).convert("RGB")
                img = img.resize((150, 150), PILImage.LANCZOS)
                buf = io.BytesIO()
                img.save(buf, format="JPEG", quality=85, subsampling=0)
                buf.seek(0)
                _cached_album_art = buf.getvalue()
            else:
                _cached_album_art = None
        except Exception:
            _cached_album_art = None


def get_real_time_current_pos():
    #Estimate current playback position smoothly.
    if not _cached_track_info:
        return "0:00"
    elapsed = time.time() - _last_position_timestamp
    current_pos = _cached_track_info.get("last_fetched_pos", 0)
    if _cached_track_info.get("state") == "Playing":
        current_pos += elapsed
    duration = 0
    try:
        dm, ds = map(int, _cached_track_info.get("duration", "0:00").split(":"))
        duration = dm * 60 + ds
    except:
        pass
    current_pos = min(current_pos, duration)
    return format_time(current_pos)

# --- Routes ---
@app.route("/nowplaying")
async def now_playing():
    base_url = request.host_url.rstrip("/") 
    data = {
        "title": _cached_track_info.get("title") if _cached_track_info else None,
        "artist": _cached_track_info.get("artist") if _cached_track_info else None,
        "current_time": get_real_time_current_pos(),
        "duration": _cached_track_info.get("duration") if _cached_track_info else None,
        "album_art": f"{base_url}/albumart",
        "state": _cached_track_info.get("state") if _cached_track_info else "Stopped"
    }
    return jsonify(data)

@app.route("/albumart")
async def album_art():
    if _cached_album_art:
        return await send_file(
            io.BytesIO(_cached_album_art), mimetype="image/jpeg"
        )
    return jsonify({"error": "No album art"}), 404

# --- Background updater ---
async def periodic_update():
    while not shutdown_event.is_set():
        await update_track_cache()
        await asyncio.sleep(CACHE_DURATION)

# --- Server ---
async def run_server():
    config = Config()
    config.bind = ["0.0.0.0:5000"]
    asyncio.create_task(periodic_update())  # run updater in background
    await serve(app, config, shutdown_trigger=shutdown_event.wait)

def server_thread():
    asyncio.run(run_server())

# --- Tray ---
def create_image():
    img = Image.new("RGB", (64, 64), "black")
    d = ImageDraw.Draw(img)
    d.rectangle([16, 16, 48, 48], fill="white")
    return img

def on_quit(icon, item):
    shutdown_event.set()
    icon.stop()
    sys.exit(0)

def run_tray():
    icon = pystray.Icon("NowPlaying", create_image(), menu=pystray.Menu(
        pystray.MenuItem("Quit", on_quit)
    ))
    icon.run()

# --- Main ---
if __name__ == "__main__":
    t = threading.Thread(target=server_thread, daemon=True)
    t.start()
    run_tray()
    t.join()
