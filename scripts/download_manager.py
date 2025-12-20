#!/usr/bin/env python3
"""
Enhanced Multi-Source Downloader with Progress & Resume
Supports: BunnyCDN, Google Drive, Google Docs, Dropbox, and direct URLs

Features:
- Smart URL extraction from any text format
- Parallel downloads with progress tracking
- Resume capability and download history
- Multiple source handlers
- Invalid URL detection and skipping
- JSON progress output for Qt integration
"""

import re
import sys
import os
import json
import time
import argparse
from hashlib import md5
from html import unescape
from urllib.parse import urlparse, parse_qs, unquote
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed
import requests
import mimetypes

# Global flag for JSON progress output
JSON_PROGRESS = False

# Try to import optional dependencies
try:
    from tqdm import tqdm
    HAS_TQDM = True
except ImportError:
    HAS_TQDM = False

try:
    import yt_dlp
    HAS_YT_DLP = True
except ImportError:
    HAS_YT_DLP = False
    if not JSON_PROGRESS:
        print("[WARN] yt-dlp not installed. Install with: pip install yt-dlp")

try:
    from colorama import init, Fore, Style
    init()
    HAS_COLOR = True
except ImportError:
    HAS_COLOR = False
    # Create dummy color constants
    class Fore:
        GREEN = YELLOW = RED = CYAN = RESET = ''
    class Style:
        BRIGHT = RESET_ALL = ''

# Configuration
CONFIG_FILE = "bunny_downloader_config.json"
HISTORY_FILE = "download_history.json"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FFMPEG_PATH = os.path.join(SCRIPT_DIR, 'ffmpeg.exe') if os.name == 'nt' else 'ffmpeg'
FFPROBE_PATH = os.path.join(SCRIPT_DIR, 'ffprobe.exe') if os.name == 'nt' else 'ffprobe'

# Default configuration
DEFAULT_CONFIG = {
    "max_parallel_downloads": 3,
    "retry_attempts": 3,
    "retry_delay": 5,
    "preferred_quality": "highest",
    "download_subtitles": True,
    "skip_existing": True,
    "create_info_json": False,
    "output_format": "{title}.mp4",
    "google_docs_format": "pdf"
}


def emit_progress(file, percent, speed="", eta=""):
    """Emit JSON progress for Qt wrapper"""
    if JSON_PROGRESS:
        progress = {
            "type": "progress",
            "file": file,
            "percent": int(percent),
            "speed": speed,
            "eta": eta
        }
        print(json.dumps(progress), flush=True)


def emit_complete(file, path):
    """Emit JSON completion for Qt wrapper"""
    if JSON_PROGRESS:
        complete = {
            "type": "complete",
            "file": file,
            "path": path
        }
        print(json.dumps(complete), flush=True)


def emit_error(file, error):
    """Emit JSON error for Qt wrapper"""
    if JSON_PROGRESS:
        err = {
            "type": "error",
            "file": file,
            "error": error
        }
        print(json.dumps(err), flush=True)


class DownloadProgress:
    """Progress callback handler for yt-dlp"""
    def __init__(self, filename):
        self.filename = filename
        self.pbar = None
        self.last_bytes = 0
        self.initialized = False
        self.last_printed = 0

    def __call__(self, d):
        if d['status'] == 'downloading':
            # For fragmented downloads, use fragment info
            if 'fragment_index' in d and 'fragment_count' in d:
                frag_index = d.get('fragment_index', 0)
                frag_count = d.get('fragment_count', 0)

                if frag_count > 0:
                    percent = (frag_index / frag_count) * 100
                    downloaded = d.get('downloaded_bytes', 0)
                    speed = d.get('speed', 0)
                    eta = d.get('eta', 0)

                    # Format output
                    size_str = self._format_bytes(downloaded)
                    speed_str = f"{self._format_bytes(speed)}/s" if speed else "N/A"
                    eta_str = self._format_time(eta) if eta else "N/A"

                    if JSON_PROGRESS:
                        emit_progress(self.filename, percent, speed_str, eta_str)
                    else:
                        print(f"\r[{self.filename[:30]}...] Fragment {frag_index}/{frag_count} "
                              f"({percent:.1f}%) - {size_str} at {speed_str} ETA: {eta_str}      ",
                              end='', flush=True)
                return

            # For non-fragmented downloads
            percent = d.get('_percent_str', '0%').strip().replace('%', '')
            try:
                percent = float(percent)
            except:
                percent = 0

            speed = d.get('_speed_str', 'N/A').strip()
            eta = d.get('_eta_str', 'N/A').strip()

            if JSON_PROGRESS:
                emit_progress(self.filename, percent, speed, eta)
            else:
                if HAS_TQDM and not self.initialized:
                    total = d.get('total_bytes') or d.get('total_bytes_estimate')
                    if total and total > 1024 * 1024:
                        self.pbar = tqdm(
                            desc=self.filename[:50],
                            total=total,
                            unit='B',
                            unit_scale=True,
                            unit_divisor=1024
                        )
                        self.initialized = True

                if self.pbar and self.initialized:
                    downloaded = d.get('downloaded_bytes', 0)
                    if downloaded > self.last_bytes:
                        self.pbar.update(downloaded - self.last_bytes)
                        self.last_bytes = downloaded
                else:
                    total = d.get('total_bytes_estimate_str', d.get('total_bytes_str', 'Unknown'))
                    current_time = time.time()
                    if current_time - self.last_printed > 0.5:
                        print(f"\r[{self.filename[:30]}...] {percent:.0f}% of {total} at {speed} ETA: {eta}      ",
                              end='', flush=True)
                        self.last_printed = current_time

        elif d['status'] == 'finished':
            if self.pbar:
                self.pbar.close()
            if not JSON_PROGRESS:
                print()

    def _format_bytes(self, bytes):
        """Format bytes to human readable string"""
        for unit in ['B', 'KB', 'MB', 'GB']:
            if bytes < 1024.0:
                return f"{bytes:.1f}{unit}"
            bytes /= 1024.0
        return f"{bytes:.1f}TB"

    def _format_time(self, seconds):
        """Format seconds to human readable string"""
        if seconds is None or seconds <= 0:
            return "N/A"

        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = int(seconds % 60)

        if hours > 0:
            return f"{hours:02d}:{minutes:02d}:{secs:02d}"
        else:
            return f"{minutes:02d}:{secs:02d}"


class DownloadManager:
    def __init__(self, config_file=CONFIG_FILE):
        self.config = self.load_config(config_file)
        self.history = self.load_history()
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        })

        # URL patterns for different services
        self.url_patterns = {
            'bunnycdn': re.compile(r'https://iframe\.mediadelivery\.net/embed/\d+/[a-f0-9\-]+'),
            'googledrive_file': re.compile(r'https://drive\.google\.com/file/d/([a-zA-Z0-9_-]+)'),
            'googledrive_folder': re.compile(r'https://drive\.google\.com/drive/folders/([a-zA-Z0-9_-]+)'),
            'googledocs': re.compile(r'https://docs\.google\.com/(document|spreadsheets|presentation)/d/([a-zA-Z0-9_-]+)'),
            'dropbox': re.compile(r'https://.*?dropbox.*?\.com/.*?\.(mp4|mp3|pdf|zip|mov|avi|mkv|doc|docx|xls|xlsx)', re.IGNORECASE),
            'generic_media': re.compile(r'https?://.*?\.(mp4|mp3|pdf|zip|mov|avi|mkv|doc|docx|xls|xlsx)', re.IGNORECASE)
        }

    def extract_urls_from_text(self, text):
        """Extract all URLs from text, handling various formats"""
        url_pattern = re.compile(
            r'https?://(?:[a-zA-Z]|[0-9]|[$-_@.&+]|[!*\\(\\),]|(?:%[0-9a-fA-F][0-9a-fA-F]))+'
        )

        urls = []
        for match in url_pattern.finditer(text):
            url = match.group(0)
            url = re.sub(r'[.,;:!?\s]+$', '', url)
            if url.count('(') < url.count(')'):
                url = url.rstrip(')')
            urls.append(url)

        seen = set()
        unique_urls = []
        for url in urls:
            if url not in seen:
                seen.add(url)
                unique_urls.append(url)

        return unique_urls

    def detect_url_type(self, url):
        """Detect the type of URL and whether it's valid"""
        if 'iframe.mediadelivery.net/embed/' in url:
            match = re.search(r'/embed/(\d+)/([a-f0-9\-]+)', url)
            if match and match.group(2):
                return 'bunnycdn', True
            else:
                return 'bunnycdn', False

        if self.url_patterns['googledrive_file'].search(url):
            return 'googledrive_file', True

        if self.url_patterns['googledrive_folder'].search(url):
            return 'googledrive_folder', True

        if self.url_patterns['googledocs'].search(url):
            return 'googledocs', True

        if 'dropbox' in url.lower():
            return 'dropbox', True

        if self.url_patterns['generic_media'].search(url):
            return 'generic_media', True

        return 'unknown', False

    def get_google_drive_direct_link(self, url):
        """Convert Google Drive sharing link to direct download link"""
        file_id_match = self.url_patterns['googledrive_file'].search(url)
        if file_id_match:
            file_id = file_id_match.group(1)
            return f"https://drive.google.com/uc?export=download&id={file_id}", file_id
        return None, None

    def get_google_docs_export_link(self, url, export_format='pdf'):
        """Convert Google Docs/Sheets/Slides link to export link"""
        match = self.url_patterns['googledocs'].search(url)
        if match:
            doc_type = match.group(1)
            doc_id = match.group(2)

            export_formats = {
                'document': {'pdf': 'pdf', 'docx': 'docx', 'txt': 'txt', 'html': 'html'},
                'spreadsheets': {'pdf': 'pdf', 'xlsx': 'xlsx', 'csv': 'csv'},
                'presentation': {'pdf': 'pdf', 'pptx': 'pptx'}
            }

            if doc_type in export_formats and export_format in export_formats[doc_type]:
                if doc_type == 'spreadsheets':
                    return f"https://docs.google.com/spreadsheets/d/{doc_id}/export?format={export_format}", doc_id
                else:
                    return f"https://docs.google.com/{doc_type}/d/{doc_id}/export?format={export_format}", doc_id

        return None, None

    def load_config(self, config_file):
        """Load configuration from file or create default"""
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    config = json.load(f)
                    for key, value in DEFAULT_CONFIG.items():
                        if key not in config:
                            config[key] = value
                    return config
            except:
                pass
        return DEFAULT_CONFIG.copy()

    def load_history(self):
        """Load download history"""
        if os.path.exists(HISTORY_FILE):
            try:
                with open(HISTORY_FILE, 'r') as f:
                    return json.load(f)
            except:
                pass
        return {}

    def save_history(self):
        """Save download history"""
        try:
            with open(HISTORY_FILE, 'w') as f:
                json.dump(self.history, f, indent=2)
        except:
            pass

    def is_downloaded(self, url):
        """Check if URL was already downloaded successfully"""
        return self.config['skip_existing'] and url in self.history and self.history[url].get('status') == 'completed'

    def mark_downloaded(self, url, filename, status='completed', folder=None):
        """Mark URL as downloaded"""
        self.history[url] = {
            'filename': filename,
            'status': status,
            'timestamp': datetime.now().isoformat(),
            'folder': folder
        }
        self.save_history()

    def sanitize_filename(self, filename):
        """Remove invalid characters from filename"""
        invalid_chars = '<>:"|?*\\/'
        for char in invalid_chars:
            filename = filename.replace(char, '_')
        filename = filename.strip('. ')
        name, ext = os.path.splitext(filename)
        if len(name) > 200:
            name = name[:200]
        return name + ext

    def check_for_drm(self, embed_page):
        """Check if the video uses DRM"""
        drm_patterns = [
            r'contextId=([\w-]+)&amp;secret=([\w-]+)',
            r'contextId=([\w-]+)&secret=([\w-]+)',
            r'\.drm/',
            r'playlist\.drm',
        ]

        for pattern in drm_patterns:
            if re.search(pattern, embed_page):
                return True
        return False

    def extract_video_info(self, embed_url, embed_page):
        """Extract video information from embed page"""
        info = {
            'title': None,
            'duration': None,
            'upload_date': None,
            'description': None,
            'thumbnail': None
        }

        title_match = re.search(r'og:title"\s*content="([^"]+)"', embed_page)
        if title_match:
            info['title'] = unescape(title_match.group(1))

        duration_match = re.search(r'video:duration"\s*content="(\d+)"', embed_page)
        if duration_match:
            info['duration'] = int(duration_match.group(1))

        date_match = re.search(r'"uploadDate":\s*"([^"]+)"', embed_page)
        if date_match:
            info['upload_date'] = date_match.group(1)

        desc_match = re.search(r'og:description"\s*content="([^"]+)"', embed_page)
        if desc_match:
            info['description'] = unescape(desc_match.group(1))

        thumb_match = re.search(r'og:image"\s*content="([^"]+)"', embed_page)
        if thumb_match:
            info['thumbnail'] = thumb_match.group(1)

        return info

    def format_filename(self, info, embed_url):
        """Format filename based on template"""
        guid = urlparse(embed_url).path.split("/")[-1].split("?")[0]
        title = info.get('title', guid)
        if title.endswith('.mp4'):
            title = title[:-4]
        return self.sanitize_filename(f"{title}.mp4")

    def download_with_retry(self, download_func, *args, **kwargs):
        """Execute download with retry logic"""
        last_error = None
        for attempt in range(self.config['retry_attempts']):
            try:
                return download_func(*args, **kwargs)
            except Exception as e:
                last_error = e
                if attempt < self.config['retry_attempts'] - 1:
                    wait_time = self.config['retry_delay'] * (attempt + 1)
                    if not JSON_PROGRESS:
                        print(f"{Fore.YELLOW}[RETRY] Attempt {attempt + 1} failed, waiting {wait_time}s...{Style.RESET_ALL}")
                    time.sleep(wait_time)

        raise last_error

    def download_google_drive_file(self, url, dest_folder):
        """Download file from Google Drive"""
        direct_link, file_id = self.get_google_drive_direct_link(url)
        if not direct_link:
            raise Exception("Invalid Google Drive URL")

        if not JSON_PROGRESS:
            print(f"{Fore.CYAN}[INFO] Downloading Google Drive file: {file_id}{Style.RESET_ALL}")

        response = self.session.get(direct_link, stream=True, allow_redirects=True)

        # Handle virus scan warning for large files
        if 'virus scan warning' in response.text.lower() or response.headers.get('Content-Type', '').startswith('text/html'):
            confirm_token = None
            for key, value in response.cookies.items():
                if key.startswith('download_warning'):
                    confirm_token = value
                    break

            if not confirm_token:
                token_match = re.search(r'confirm=([0-9A-Za-z_-]+)', response.text)
                if token_match:
                    confirm_token = token_match.group(1)

            if confirm_token:
                params = {'id': file_id, 'export': 'download', 'confirm': confirm_token}
                response = self.session.get("https://drive.google.com/uc", params=params, stream=True, cookies=response.cookies)
            else:
                alt_url = f"https://drive.google.com/uc?export=download&id={file_id}&confirm=t"
                response = self.session.get(alt_url, stream=True)

        # Get filename
        filename = None
        content_disposition = response.headers.get('Content-Disposition')
        if content_disposition:
            filename_match = re.search(r'filename[^;=\n]*=(([\'"]).*?\2|[^;\n]*)', content_disposition)
            if filename_match:
                filename = filename_match.group(1).strip('"\'')

        if not filename:
            content_type = response.headers.get('Content-Type', '')
            filename = f"googledrive_{file_id}"
            ext = mimetypes.guess_extension(content_type.split(';')[0])
            if ext:
                filename += ext

        filename = self.sanitize_filename(filename)
        filepath = os.path.join(dest_folder, filename)

        total_size = int(response.headers.get('Content-Length', 0))

        with open(filepath, 'wb') as f:
            downloaded = 0
            for chunk in response.iter_content(chunk_size=32768):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        size_mb = downloaded / (1024 * 1024)
                        emit_progress(filename, percent, f"{size_mb:.1f}MB", "")
                        if not JSON_PROGRESS:
                            print(f"\r[Google Drive] {percent:.1f}%", end='', flush=True)

        if not JSON_PROGRESS:
            print()
        emit_complete(filename, filepath)
        return filename

    def download_google_docs(self, url, dest_folder, export_format='pdf'):
        """Download Google Docs/Sheets/Slides as specified format"""
        export_link, doc_id = self.get_google_docs_export_link(url, export_format)
        if not export_link:
            raise Exception("Invalid Google Docs URL")

        if not JSON_PROGRESS:
            print(f"{Fore.CYAN}[INFO] Downloading Google Docs: {doc_id} as {export_format}{Style.RESET_ALL}")

        response = self.session.get(export_link, stream=True)
        response.raise_for_status()

        filename = f"googledocs_{doc_id}.{export_format}"
        content_disposition = response.headers.get('Content-Disposition')
        if content_disposition:
            filename_match = re.search(r'filename="?([^"]+)"?', content_disposition)
            if filename_match:
                filename = filename_match.group(1)

        filename = self.sanitize_filename(filename)
        filepath = os.path.join(dest_folder, filename)

        total_size = int(response.headers.get('Content-Length', 0))
        downloaded = 0
        with open(filepath, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        emit_progress(filename, percent, "", "")
                        if not JSON_PROGRESS:
                            print(f"\r[Google Docs] {percent:.1f}%", end='', flush=True)

        if not JSON_PROGRESS:
            print()
        emit_complete(filename, filepath)
        return filename

    def download_dropbox_file(self, url, dest_folder):
        """Download file from Dropbox"""
        if '?dl=0' in url:
            url = url.replace('?dl=0', '?dl=1')
        elif '&dl=0' in url:
            url = url.replace('&dl=0', '&dl=1')
        elif '?dl=1' not in url and '&dl=1' not in url:
            separator = '&' if '?' in url else '?'
            url = f"{url}{separator}dl=1"

        if not JSON_PROGRESS:
            print(f"{Fore.CYAN}[INFO] Downloading Dropbox file{Style.RESET_ALL}")

        response = self.session.get(url, stream=True)
        response.raise_for_status()

        filename = None
        content_disposition = response.headers.get('Content-Disposition')
        if content_disposition:
            filename_match = re.search(r'filename="?([^"]+)"?', content_disposition)
            if filename_match:
                filename = filename_match.group(1)

        if not filename:
            parsed_url = urlparse(url)
            filename = os.path.basename(parsed_url.path)
            if not filename or filename == 'dl':
                filename = f"dropbox_download_{int(time.time())}"

        filename = self.sanitize_filename(unquote(filename))
        filepath = os.path.join(dest_folder, filename)

        total_size = int(response.headers.get('Content-Length', 0))
        with open(filepath, 'wb') as f:
            downloaded = 0
            for chunk in response.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        emit_progress(filename, percent, "", "")
                        if not JSON_PROGRESS:
                            print(f"\r[Dropbox] {percent:.1f}%", end='', flush=True)

        if not JSON_PROGRESS:
            print()
        emit_complete(filename, filepath)
        return filename

    def download_generic_file(self, url, dest_folder):
        """Download generic media file"""
        if not JSON_PROGRESS:
            print(f"{Fore.CYAN}[INFO] Downloading: {url}{Style.RESET_ALL}")

        response = self.session.get(url, stream=True)
        response.raise_for_status()

        # Get filename from URL or headers
        filename = None
        content_disposition = response.headers.get('Content-Disposition')
        if content_disposition:
            filename_match = re.search(r'filename="?([^"]+)"?', content_disposition)
            if filename_match:
                filename = filename_match.group(1)

        if not filename:
            parsed_url = urlparse(url)
            filename = os.path.basename(parsed_url.path)
            if '?' in filename:
                filename = filename.split('?')[0]

        if not filename:
            filename = f"download_{int(time.time())}"

        filename = self.sanitize_filename(unquote(filename))
        filepath = os.path.join(dest_folder, filename)

        total_size = int(response.headers.get('Content-Length', 0))
        with open(filepath, 'wb') as f:
            downloaded = 0
            for chunk in response.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        emit_progress(filename, percent, "", "")
                        if not JSON_PROGRESS:
                            print(f"\r[HTTP] {percent:.1f}%", end='', flush=True)

        if not JSON_PROGRESS:
            print()
        emit_complete(filename, filepath)
        return filename

    def download_non_drm_video(self, embed_url, embed_page, info, dest_folder):
        """Download non-DRM protected video"""
        playlist_url = None
        patterns = [
            r'<source\s+src="([^"]+playlist\.m3u8[^"]*)"',
            r'["\'](https://[^"\']+playlist\.m3u8[^"\']*)["\']',
        ]

        for pattern in patterns:
            match = re.search(pattern, embed_page)
            if match:
                playlist_url = match.group(1)
                break

        if not playlist_url:
            raise Exception("Could not find playlist URL")

        filename = self.format_filename(info, embed_url)
        output_path = os.path.join(dest_folder, filename)

        if HAS_YT_DLP:
            progress = DownloadProgress(filename)
            ydl_opts = {
                'outtmpl': output_path,
                'quiet': True,
                'no_warnings': True,
                'progress_hooks': [progress],
                'writesubtitles': self.config['download_subtitles'],
                'subtitlesformat': 'vtt',
                'http_headers': {
                    'Origin': 'https://iframe.mediadelivery.net',
                    'Referer': 'https://iframe.mediadelivery.net/',
                    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
                }
            }

            if self.config['preferred_quality'] != 'highest':
                if self.config['preferred_quality'] == 'lowest':
                    ydl_opts['format'] = 'worst[ext=mp4]/worst'
                elif self.config['preferred_quality'].isdigit():
                    height = int(self.config['preferred_quality'])
                    ydl_opts['format'] = f'best[height<={height}]/best'

            with yt_dlp.YoutubeDL(ydl_opts) as ydl:
                ydl.download([embed_url])
        else:
            # Fallback to ffmpeg
            import subprocess
            headers = (
                "Origin: https://iframe.mediadelivery.net\r\n"
                "Referer: https://iframe.mediadelivery.net/\r\n"
            )
            cmd = [
                'ffmpeg',
                '-headers', headers,
                '-i', playlist_url,
                '-c', 'copy',
                '-y',
                output_path,
                '-hide_banner',
                '-loglevel', 'error'
            ]
            subprocess.run(cmd, check=True)

        emit_complete(filename, output_path)
        return filename

    def download_drm_video(self, embed_url, embed_page, info, dest_folder):
        """Download DRM-protected video"""
        if not HAS_YT_DLP:
            raise Exception("yt-dlp is required for DRM videos")

        guid = urlparse(embed_url).path.split("/")[-1].split("?")[0]

        server_match = re.search(r"https://video-(.*?)\.mediadelivery\.net", embed_page)
        if not server_match:
            raise Exception("Could not find server ID")
        server_id = server_match.group(1)

        patterns = [
            r'contextId=([\w-]+)&amp;secret=([\w-]+)',
            r'contextId=([\w-]+)&secret=([\w-]+)',
        ]

        context_id = secret = None
        for pattern in patterns:
            match = re.search(pattern, embed_page)
            if match:
                context_id = match.group(1)
                secret = match.group(2)
                break

        if not context_id or not secret:
            raise Exception("Could not find DRM parameters")

        self._drm_auth(server_id, context_id, secret, guid)

        resolution = self._get_best_resolution(guid, context_id, secret)
        url = f"https://iframe.mediadelivery.net/{guid}/{resolution}/video.drm?contextId={context_id}"

        filename = self.format_filename(info, embed_url)
        output_path = os.path.join(dest_folder, filename)

        progress = DownloadProgress(filename)
        ydl_opts = {
            "http_headers": {
                "Origin": "https://iframe.mediadelivery.net",
                "Referer": embed_url,
                "User-Agent": self.session.headers['User-Agent'],
            },
            "outtmpl": output_path,
            "quiet": True,
            'progress_hooks': [progress],
        }
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            ydl.download([url])

        emit_complete(filename, output_path)
        return filename

    def _drm_auth(self, server_id, context_id, secret, guid):
        """Perform DRM authentication"""
        def ping(time_val, paused, res):
            md5_hash = md5(f"{secret}_{context_id}_{time_val}_{paused}_{res}".encode()).hexdigest()
            params = {"hash": md5_hash, "time": time_val, "paused": paused, "chosen_res": res}
            self.session.get(f"https://video-{server_id}.mediadelivery.net/.drm/{context_id}/ping", params=params)

        ping(0, "true", "0")
        self.session.get(f"https://video-{server_id}.mediadelivery.net/.drm/{context_id}/activate")

    def _get_best_resolution(self, guid, context_id, secret):
        """Get best available resolution"""
        playlist_url = f"https://iframe.mediadelivery.net/{guid}/playlist.drm"
        params = {"contextId": context_id, "secret": secret}
        response = self.session.get(playlist_url, params=params)

        resolutions = re.findall(r'(\d+x\d+)/video\.drm', response.text)
        if not resolutions:
            raise Exception("No resolutions found")

        resolutions = sorted(resolutions, key=lambda x: int(x.split('x')[1]), reverse=True)
        return resolutions[0]

    def download_video(self, url, dest_folder):
        """Main download function that routes to appropriate handler"""
        url_type, is_valid = self.detect_url_type(url)

        if not is_valid and url_type != 'unknown':
            if not JSON_PROGRESS:
                print(f"{Fore.YELLOW}[SKIP] Invalid {url_type} URL: {url}{Style.RESET_ALL}")
            emit_error(url, f"Invalid {url_type} URL")
            return False

        if self.is_downloaded(url):
            filename = self.history[url].get('filename', 'Unknown')
            if not JSON_PROGRESS:
                print(f"{Fore.GREEN}[SKIP] Already downloaded: {filename}{Style.RESET_ALL}")
            emit_complete(filename, os.path.join(dest_folder, filename))
            return True

        try:
            if url_type == 'bunnycdn':
                return self.download_bunnycdn_video(url, dest_folder)
            elif url_type == 'googledrive_file':
                filename = self.download_with_retry(
                    self.download_google_drive_file, url, dest_folder
                )
                self.mark_downloaded(url, filename, folder=dest_folder)
                if not JSON_PROGRESS:
                    print(f"{Fore.GREEN}[OK] Downloaded: {filename}{Style.RESET_ALL}")
                return True
            elif url_type == 'googledocs':
                export_format = self.config.get('google_docs_format', 'pdf')
                filename = self.download_with_retry(
                    self.download_google_docs, url, dest_folder, export_format
                )
                self.mark_downloaded(url, filename, folder=dest_folder)
                if not JSON_PROGRESS:
                    print(f"{Fore.GREEN}[OK] Downloaded: {filename}{Style.RESET_ALL}")
                return True
            elif url_type == 'dropbox':
                filename = self.download_with_retry(
                    self.download_dropbox_file, url, dest_folder
                )
                self.mark_downloaded(url, filename, folder=dest_folder)
                if not JSON_PROGRESS:
                    print(f"{Fore.GREEN}[OK] Downloaded: {filename}{Style.RESET_ALL}")
                return True
            elif url_type == 'googledrive_folder':
                if not JSON_PROGRESS:
                    print(f"{Fore.YELLOW}[INFO] Google Drive folders not supported yet{Style.RESET_ALL}")
                emit_error(url, "Google Drive folders not supported")
                return False
            elif url_type == 'generic_media':
                filename = self.download_with_retry(
                    self.download_generic_file, url, dest_folder
                )
                self.mark_downloaded(url, filename, folder=dest_folder)
                if not JSON_PROGRESS:
                    print(f"{Fore.GREEN}[OK] Downloaded: {filename}{Style.RESET_ALL}")
                return True
            else:
                if not JSON_PROGRESS:
                    print(f"{Fore.YELLOW}[SKIP] Unsupported URL type: {url}{Style.RESET_ALL}")
                emit_error(url, "Unsupported URL type")
                return False

        except Exception as e:
            if not JSON_PROGRESS:
                print(f"{Fore.RED}[ERROR] Failed to download {url}: {str(e)}{Style.RESET_ALL}")
            emit_error(url, str(e))
            self.mark_downloaded(url, str(e), status='failed', folder=dest_folder)
            return False

    def download_bunnycdn_video(self, embed_url, dest_folder):
        """BunnyCDN download logic"""
        try:
            response = self.session.get(embed_url)
            response.raise_for_status()
            embed_page = response.text

            info = self.extract_video_info(embed_url, embed_page)
            is_drm = self.check_for_drm(embed_page)

            if not JSON_PROGRESS:
                print(f"{Fore.CYAN}[INFO] Title: {info.get('title', 'Unknown')}{Style.RESET_ALL}")
                print(f"{Fore.CYAN}[INFO] DRM: {'Yes' if is_drm else 'No'}{Style.RESET_ALL}")

            if is_drm:
                filename = self.download_with_retry(
                    self.download_drm_video, embed_url, embed_page, info, dest_folder
                )
            else:
                filename = self.download_with_retry(
                    self.download_non_drm_video, embed_url, embed_page, info, dest_folder
                )

            self.mark_downloaded(embed_url, filename, folder=dest_folder)

            file_path = os.path.join(dest_folder, filename)
            if os.path.exists(file_path):
                size_mb = os.path.getsize(file_path) / (1024 * 1024)
                if not JSON_PROGRESS:
                    print(f"{Fore.GREEN}[OK] Downloaded: {filename} ({size_mb:.1f} MB){Style.RESET_ALL}")

            return True

        except Exception as e:
            if not JSON_PROGRESS:
                print(f"{Fore.RED}[ERROR] Failed to download {embed_url}: {str(e)}{Style.RESET_ALL}")
            emit_error(embed_url, str(e))
            self.mark_downloaded(embed_url, str(e), status='failed', folder=dest_folder)
            return False


def main():
    global JSON_PROGRESS

    parser = argparse.ArgumentParser(
        description='Enhanced Multi-Source Downloader - Supports BunnyCDN, Google Drive, Dropbox, and more!'
    )
    parser.add_argument('--url', help='Single URL to download')
    parser.add_argument('-i', '--input', help='Input file with URLs')
    parser.add_argument('-o', '--output', default='Downloads', help='Output directory')
    parser.add_argument('-p', '--parallel', type=int, default=1, help='Number of parallel downloads')
    parser.add_argument('-q', '--quality', choices=['highest', 'lowest', '1080', '720', '480'],
                       default='highest', help='Preferred quality for videos')
    parser.add_argument('--skip-existing', action='store_true', default=True,
                       help='Skip files that were already downloaded')
    parser.add_argument('--subtitles', action='store_true', default=True,
                       help='Download subtitles if available')
    parser.add_argument('--docs-format', choices=['pdf', 'docx', 'xlsx', 'txt', 'html'],
                       default='pdf', help='Export format for Google Docs')
    parser.add_argument('--json-progress', action='store_true',
                       help='Output progress as JSON (for Qt integration)')

    args = parser.parse_args()

    # Set global JSON progress flag
    JSON_PROGRESS = args.json_progress

    # Create output directory
    os.makedirs(args.output, exist_ok=True)

    # Initialize manager with config
    manager = DownloadManager()
    manager.config['skip_existing'] = args.skip_existing
    manager.config['preferred_quality'] = args.quality
    manager.config['download_subtitles'] = args.subtitles
    manager.config['google_docs_format'] = args.docs_format

    # Determine URLs to download
    urls = []
    if args.url:
        urls = [args.url]
    elif args.input and os.path.isfile(args.input):
        with open(args.input, 'r', encoding='utf-8') as f:
            content = f.read()
        urls = manager.extract_urls_from_text(content)
    else:
        if not JSON_PROGRESS:
            print("Error: Either --url or --input must be provided")
        sys.exit(1)

    if not urls:
        if not JSON_PROGRESS:
            print("No valid URLs found")
        sys.exit(1)

    # Download
    successful = 0
    failed = 0

    for url in urls:
        if manager.download_video(url, args.output):
            successful += 1
        else:
            failed += 1

    if not JSON_PROGRESS:
        print(f"\nCompleted: {successful} success, {failed} failed")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
