#!/usr/bin/env python3
"""NEEBLES Q06. Stage-only controlled source modifier for LibVNCClient 0.9.15.
No packages, sysroot, services, Git commits, or deployed binaries are changed.
The user supplies a verified libvncserver source tree with upstream/debian patches.
"""
import argparse
import hashlib
import sys
from pathlib import Path

def transform(text: str, before: str, after: str, path: Path) -> str:
    if after in text:
        return text
    if text.count(before) != 1:
        raise SystemExit(f'FAIL CLOSED: expected unique source anchor in {path}')
    return text.replace(before, after)

HEADER_ANCHOR = 'struct _rfbClient;\n'
HEADER_NEW = '''#define NEEBLES_RFB_TOPOLOGY_V1 1
/* Host-byte-order ExtendedDesktopSize descriptors; borrowed for callback duration. */
typedef void (*rfbNeeblesScreenLayoutProc)(struct _rfbClient* client,
    unsigned int reason, unsigned int result, int fbWidth, int fbHeight,
    unsigned int count, const rfbExtDesktopScreen* screens);

extern int rfbNeeblesTopologyApiVersion(void);

struct _rfbClient;
'''
# forward declaration must exist before callback typedef; use a callback insert after forward decl
HEADER_NEW = '''struct _rfbClient;
#define NEEBLES_RFB_TOPOLOGY_V1 1
/* Layout descriptors are converted to host byte order; copy during callback. */
typedef void (*rfbNeeblesScreenLayoutProc)(struct _rfbClient* client,
    unsigned int reason, unsigned int result, int fbWidth, int fbHeight,
    unsigned int count, const rfbExtDesktopScreen* screens);
extern int rfbNeeblesTopologyApiVersion(void);
'''
TAIL_ANCHOR = '        rfbBool isUpdateRectManagedByLib;\n} rfbClient;'
TAIL_NEW = '''        rfbBool isUpdateRectManagedByLib;
        /* Appended to retain all existing field offsets. Requires v1 patched library. */
        rfbNeeblesScreenLayoutProc GotNeeblesScreenLayout;
} rfbClient;'''
SOURCE_API_ANCHOR = 'rfbClientProtocolExtension* rfbClientExtensions = NULL;'
SOURCE_API_NEW = '''/* Marker linked by Q06 Connect; stock Debian libvncclient must fail to link. */
int rfbNeeblesTopologyApiVersion(void) { return 1; }

rfbClientProtocolExtension* rfbClientExtensions = NULL;'''
SOURCE_BLOCK_ANCHOR = '''        screens = eds.numberOfScreens;
        for (loop=0; loop < screens; loop++)
        {
          if (!ReadFromRFBServer(client, ((char *)&screen), sz_rfbExtDesktopScreen)) {
            return FALSE;
          }
          if (screen.id != 0 && screen.width && screen.height) {
            client->screen = screen;
          } else {
            invalidScreen = TRUE;
          }
        }

        if (!invalidScreen && (client->width != rect.r.w || client->height != rect.r.h)) {
          if(!ResizeClientBuffer(client, rect.r.w, rect.r.h)) {
            return FALSE;
          }
          rfbClientLog("Updated desktop size: %dx%d\\n", rect.r.w, rect.r.h);
        }
        client->requestedResize = FALSE;

        continue;'''
SOURCE_BLOCK_NEW = '''        /* Preserve every screen instead of overwriting client->screen.
         * Maximum RFB wire count is uint8_t (255). Never send SetDesktopSize.
         * Keep legacy screen assignment for existing libvncclient consumers.
         */
        rfbExtDesktopScreen layouts[255];
        screens = eds.numberOfScreens;
        for (loop=0; loop < screens; loop++)
        {
          if (!ReadFromRFBServer(client, ((char *)&screen), sz_rfbExtDesktopScreen)) {
            return FALSE;
          }
          if (screen.id != 0 && screen.width && screen.height) {
            client->screen = screen;
          } else {
            invalidScreen = TRUE;
          }
          layouts[loop].id = rfbClientSwap32IfLE(screen.id);
          layouts[loop].x = rfbClientSwap16IfLE(screen.x);
          layouts[loop].y = rfbClientSwap16IfLE(screen.y);
          layouts[loop].width = rfbClientSwap16IfLE(screen.width);
          layouts[loop].height = rfbClientSwap16IfLE(screen.height);
          layouts[loop].flags = rfbClientSwap32IfLE(screen.flags);
        }

        if (!invalidScreen && (client->width != rect.r.w || client->height != rect.r.h)) {
          if(!ResizeClientBuffer(client, rect.r.w, rect.r.h)) {
            return FALSE;
          }
          rfbClientLog("Updated desktop size: %dx%d\\n", rect.r.w, rect.r.h);
        }
        client->requestedResize = FALSE;
        if (client->GotNeeblesScreenLayout) {
          client->GotNeeblesScreenLayout(client, rect.r.x, rect.r.y,
                                        client->width, client->height,
                                        invalidScreen ? 0U : (unsigned int)screens,
                                        invalidScreen ? NULL : layouts);
        }

        continue;'''

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source_root', type=Path, help='Verified unpacked libvncserver-0.9.15 source directory')
    ap.add_argument('--apply', action='store_true', help='Only source patch; not build/deploy/certify')
    args = ap.parse_args()
    root = args.source_root.resolve()
    h = root / 'include/rfb/rfbclient.h'
    c = root / 'src/libvncclient/rfbclient.c'
    if not h.is_file() or not c.is_file():
        sys.exit('Expected include/rfb/rfbclient.h and src/libvncclient/rfbclient.c')
    original = {h: h.read_text(), c: c.read_text()}
    updated = dict(original)
    updated[h] = transform(updated[h], HEADER_ANCHOR, HEADER_NEW, h)
    updated[h] = transform(updated[h], TAIL_ANCHOR, TAIL_NEW, h)
    updated[c] = transform(updated[c], SOURCE_API_ANCHOR, SOURCE_API_NEW, c)
    updated[c] = transform(updated[c], SOURCE_BLOCK_ANCHOR, SOURCE_BLOCK_NEW, c)
    for f in [h,c]:
        before = hashlib.sha256(original[f].encode()).hexdigest()
        after = hashlib.sha256(updated[f].encode()).hexdigest()
        print(f'{f.relative_to(root)}: {before} => {after}')
    if not args.apply:
        print('PREVIEW PASS (no modifications)')
        return
    if all(updated[f] == original[f] for f in updated):
        print('ALREADY APPLIED'); return
    # Atomic replacements restricted to controlled source checkout, never /opt or host paths.
    if str(root).startswith('/opt/') or str(root).startswith('/usr/'):
        sys.exit('Refusing to modify system/installed files')
    for f in [h,c]:
        if updated[f] != original[f]:
            temp = f.with_suffix(f.suffix + '.neebles-q06-stage')
            temp.write_text(updated[f]); temp.replace(f)
    print('SOURCE PATCH APPLIED. No .deb, CUSTOM manifest or runtime has been changed.')

if __name__ == '__main__': main()
