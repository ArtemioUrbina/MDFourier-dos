#ifndef PROFILE_H
#define PROFILE_H

/* MDFourier profile for each sequence, name and file, printed before
 * it plays */

#define PROFILE_MAIN_NAME   "DOS: Sound Blaster FM and PCM"
#define PROFILE_MAIN_FILE   "mdfblocks-msdos.mfn"
#define PROFILE_CMS_NAME    "DOS: Sound Blaster 2.0 and CMS"
#define PROFILE_CMS_FILE    "mdfblocks-msdos-cms.mfn"
#define PROFILE_GM_NAME     "DOS: MIDI GM"
#define PROFILE_GM_FILE     "mdfblocks-msdos-midi-gm.mfn"
#define PROFILE_MT32_NAME   "DOS: MIDI MT-32"
#define PROFILE_MT32_FILE   "mdfblocks-msdos-midi-mt32.mfn"

#define PROFILE_PRINT(name, file)   printf(" Profile: %s (%s)\n", name, file)

#endif
