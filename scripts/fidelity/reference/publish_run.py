#!/usr/bin/env python3
"""Publish a measured high-cadence Explorer reference run atomically."""
from __future__ import annotations
import json, os
from datetime import datetime, timezone
from pathlib import Path

root = Path('/home/pedro/win11-explorer-qt/.state/fidelity/reference/phase2/20261031T0022-0300')
frames_dir = root / 'frames'
timeline = json.loads((frames_dir / 'guest-timeline.json').read_text(encoding='utf-8-sig'))
analysis = json.loads((root / 'analysis.json').read_text())
frames = timeline['frames']
frame_records = [{'file': str((frames_dir / f['file']).resolve()), 'timestamp_ms': f['timestamp_ms'],
                  'region': {'x': 480, 'y': 240, 'width': 640, 'height': 400}} for f in frames]
frame_files = [Path(f['file']) for f in frame_records]
assert len(frames) == len(list(frames_dir.glob('frame_*.png')))
assert all(p.is_file() for p in frame_files)
base = str(root.resolve())
all_frames = [f['file'] for f in frame_records]
artifacts = [*all_frames, str((root/'guest-frames.zip').resolve()), str((frames_dir/'guest-timeline.json').resolve()), str((root/'analysis.json').resolve()), str((root/'contact.png').resolve())]
events = timeline['events']
menu_events = [e for e in events if e['label'].startswith('menu-')]
toolbar_events = [e for e in events if e['label'].startswith('toolbar-')]
manifest = {
 'schema_version': 1, 'run_id': root.name,
 'published_at_local': datetime.now().astimezone().isoformat(timespec='seconds'),
 'environment': {
  'build': 'Windows 10 Pro product label; DisplayVersion 26H2; build 26300; UBR 9711',
  'theme': 'light (AppsUseLightTheme=1; SystemUsesLightTheme=1)', 'language': 'pt-BR (Get-Culture; Explorer UI labels observed)',
  'dpi': {'GetDpiForWindow': 96, 'scale_percent': 100}, 'resolution_px': [1440,900],
  'client_rect_px': {'window_title':'Destination – Explorador de Arquivos','client_rect': [0,0,1123,592],
                     'client_screen_origin':[194,162], 'dwm_extended_frame':[193,162,1318,755], 'dwm_hresult':0},
  'refresh_rate_hz': None, 'capture_method':'Guest PowerShell + C# System.Drawing.Graphics.CopyFromScreen, 24bpp PNG, frames buffered in guest memory then written and downloaded after capture; guest Stopwatch timestamps; inputs sent in same interactive user process via Win32 SetCursorPos and SendInput'
 },
 'fixtures': {'root_guest':'C:\\Users\\Public\\ExplorerFidelityPhase2','used':'existing synthetic destination contents; no files modified intentionally'},
 'cases': [
  {'id':'menu-open-close-3-repetitions','status':'measured','artifacts':[str((frames_dir/'guest-timeline.json').resolve()),str((root/'analysis.json').resolve()),*all_frames],
   'measurements': {'repetitions':3,'input_events':[e for e in menu_events],
      'pointer_target_screen_px':{'Classificar':[650,274]},'frame_count':len(frames),'duration_ms':timeline['duration_ms'],'actual_frame_intervals':analysis['intervals_ms'],
      'observed_roi_change_curve':{'metric':'mean grayscale luma per frame, absolute frame delta; ROI local xyxy [325,0,520,380]','values':analysis['roi_frame_delta_luma']},
      'menus_visually_seen_in_sampled_frames':['Classificar dropdown','Mais opções flyout']},
   'limitations':['frame sampling is measured; actual median fps='+str(analysis['intervals_ms']['fps_median'])+'; short transition timing below one frame is unresolved','visual menu identification confirmed by contact-sheet frames; click-to-first-visible durations only bracketed by frame timestamps']},
  {'id':'toolbar-hover-pressed-release-3-repetitions','status':'measured','artifacts':[str((frames_dir/'guest-timeline.json').resolve()),str((root/'analysis.json').resolve()),*all_frames],
   'measurements': {'repetitions':3,'input_events':[e for e in toolbar_events], 'pointer_targets_screen_px':{'Mais opções':[856,274]},
      'frame_count':len(frames),'duration_ms':timeline['duration_ms'],'actual_frame_intervals':analysis['intervals_ms'],
      'observed_roi_change_curve':{'metric':'mean grayscale luma per frame, absolute frame delta; ROI local xyxy [325,0,520,380]','values':analysis['roi_frame_delta_luma']}},
   'limitations':['target located using UI Automation center Mais opções=(856,274); pressed visual state sampled at achieved frame cadence','no interpolation']}
 ],
 'frames': frame_records,
 'input_sequence': events,
 'capture_performance': analysis['intervals_ms'],
 'artifacts': artifacts,
 'limitations':['a first scheduled-task trial captured black frames because it ran in a non-interactive context; rejected and excluded. Published clip was captured by launching the recorder in the interactive user session and visually verified.','refresh rate remains unknown; Windows API reported 1 Hz in earlier run and was not trusted','only menu and toolbar hover/press were repeated; copy interaction and drag-and-drop are out of scope here'] ,
 'artifact_root': base
}
# Path and count validation before atomic publish.
assert len(frame_records) == len(list(frames_dir.glob('frame_*.png')),), len(frame_records)
assert len(set(all_frames)) == len(all_frames)
tmp=root/'manifest.json.tmp'
tmp.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
os.replace(tmp,root/'manifest.json')
print(json.dumps({'manifest':str(root/'manifest.json'),'frame_count':len(frame_records),'artifact_count':len(artifacts),'fps_median':analysis['intervals_ms']['fps_median'],'fps_interval_ms':{k:analysis['intervals_ms'][k] for k in ('min_ms','median_ms','max_ms')}},ensure_ascii=False))
