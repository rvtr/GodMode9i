/*-----------------------------------------------------------------
 Copyright (C) 2005 - 2013
	Michael "Chishm" Chisholm
	Dave "WinterMute" Murphy
	Claudio "sverx"

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.

------------------------------------------------------------------*/

#include "file_browse.h"
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include <nds.h>
#include <fat.h>

#include "main.h"
#include "date.h"
#include "screenshot.h"
#include "fileOperations.h"
#include "driveMenu.h"
#include "driveOperations.h"
#include "dumpOperations.h"
#include "font.h"
#include "hexEditor.h"
#include "ndsInfo.h"
#include "nitrofs.h"
#include "inifile.h"
#include "nds_loader_arm9.h"

#define ENTRIES_START_ROW 1
#define OPTIONS_ENTRIES_START_ROW 2
#define ENTRY_PAGE_LENGTH 10

static char path[PATH_MAX];

bool extension(const std::string &filename, const std::vector<std::string> &extensions) {
	for(const std::string &ext : extensions) {
		if(filename.length() > ext.length() && strcasecmp(filename.substr(filename.length() - ext.length()).data(), ext.data()) == 0)
			return true;
	}

	return false;
}

void OnKeyPressed(int key) {
	if(key > 0)
		iprintf("%c", key);
}

bool dirEntryPredicate (const DirEntry& lhs, const DirEntry& rhs) {

	if (!lhs.isDirectory && rhs.isDirectory) {
		return false;
	}
	if (lhs.isDirectory && !rhs.isDirectory) {
		return true;
	}
	return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
}

void getDirectoryContents(std::vector<DirEntry>& dirContents) {
	struct stat st;

	dirContents.clear();

	DIR *pdir = opendir (".");

	if (pdir == NULL) {
		font->print(0, 0, true, "Unable to open the directory.");
		font->update(true);
	} else {

		while(true) {
			DirEntry dirEntry;

			struct dirent* pent = readdir(pdir);
			if(pent == NULL) break;

			stat(pent->d_name, &st);
			if (strcmp(pent->d_name, "..") != 0) {
				dirEntry.name = pent->d_name;
				dirEntry.isDirectory = st.st_mode & S_IFDIR;
				if (!dirEntry.isDirectory) {
					dirEntry.size = getFileSize(dirEntry.name.c_str());
				}
				if (extension(dirEntry.name, {"nds", "argv", "dsi", "ids", "app", "srl"})) {
					dirEntry.isApp = ((currentDrive == 0 && sdMounted) || (currentDrive == 1 && flashcardMounted));
				} else if (extension(dirEntry.name, {"firm"})) {
					dirEntry.isApp = (isDSiMode() && is3DS && sdMounted);
				} else {
					dirEntry.isApp = false;
				}

				if (dirEntry.name.compare(".") != 0) {
					dirContents.push_back (dirEntry);
				}
			}

		}

		closedir(pdir);
	}

	sort(dirContents.begin(), dirContents.end(), dirEntryPredicate);

	DirEntry dirEntry;
	dirEntry.name = "..";	// ".." entry
	dirEntry.isDirectory = true;
	dirEntry.isApp = false;
	dirContents.insert (dirContents.begin(), dirEntry);	// Add ".." to top of list
}

void showDirectoryContents (const std::vector<DirEntry>& dirContents, int fileOffset, int startRow) {
	getcwd(path, PATH_MAX);

	font->clear(true);

	// Top bar
	font->printf(0, 0, true, Alignment::left, Palette::blackGreen, "%*c", 256 / font->width(), ' ');

	// Print time
	font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);

	// Print the path
	if(font->calcWidth(path) > SCREEN_COLS - 6)
		font->print(-6 - 1, 0, true, path, Alignment::right, Palette::blackGreen);
	else
		font->print(0, 0, true, path, Alignment::left, Palette::blackGreen);

	// Print directory listing
	for (int i = 0; i < ((int)dirContents.size() - startRow) && i < ENTRIES_PER_SCREEN; i++) {
		const DirEntry *entry = &dirContents[i + startRow];

		Palette pal;
		if ((fileOffset - startRow) == i) {
			pal = Palette::white;
		} else if (entry->selected) {
			pal = Palette::yellow;
		} else if (entry->isDirectory) {
			pal = Palette::blue;
		} else {
			pal = Palette::gray;
		}

		font->print(0, i + 1, true, entry->name.substr(0, SCREEN_COLS), Alignment::left, pal);
		if (entry->name == "..") {
			font->print(-1, i + 1, true, "(..)", Alignment::right, pal);
		} else if (entry->isDirectory) {
			font->print(-1, i + 1, true, " (dir)", Alignment::right, pal);
		} else {
			font->printf(-1, i + 1, true, Alignment::right, pal, " (%s)", getBytes(entry->size).c_str());
		}
	}

	font->update(true);
}

FileOperation fileBrowse_A(DirEntry* entry, char path[PATH_MAX]) {
	int pressed = 0, held = 0;
	std::vector<FileOperation> operations;
	int optionOffset = 0;
	std::string fullPath = path + entry->name;
	int y = font->calcHeight(fullPath) + 1;

	if (!entry->isDirectory) {
		if (entry->isApp) {
			operations.push_back(FileOperation::bootFile);
			if (!extension(entry->name, {"firm"})) {
				operations.push_back(FileOperation::bootstrapFile);
			}
		}

		if(extension(entry->name, {"nds", "dsi", "ids", "app"})) {
			operations.push_back(FileOperation::mountNitroFS);
			operations.push_back(FileOperation::ndsInfo);
			operations.push_back(FileOperation::trimNds);
		} else if(extension(entry->name, {"sav", "sav1", "sav2", "sav3", "sav4", "sav5", "sav6", "sav7", "sav8", "sav9"})) {
			operations.push_back(FileOperation::restoreSave);
		} else if(extension(entry->name, {"img", "sd"})) {
			operations.push_back(FileOperation::mountImg);
		} else if(extension(entry->name, {"frf"})) {
			operations.push_back(FileOperation::loadFont);
		}

		operations.push_back(FileOperation::hexEdit);

		// The bios SHA1 functions are only available on the DSi
		// https://problemkaputt.de/gbatek.htm#biossha1functionsdsionly
		if (isDSiMode()) {
			operations.push_back(FileOperation::calculateSHA1);
		}
	}

	operations.push_back(FileOperation::showInfo);

	if (sdMounted && (strcmp(path, "sd:/gm9i/out/") != 0)) {
		operations.push_back(FileOperation::copySdOut);
	}

	if (flashcardMounted && (strcmp(path, "fat:/gm9i/out/") != 0)) {
		operations.push_back(FileOperation::copyFatOut);
	}

	while (true) {
		font->clear(false);

		font->print(0, 0, false, fullPath);

		int row = y;
		for(FileOperation operation : operations) {
			switch(operation) {
				case FileOperation::bootFile:
					font->print(3, row++, false, extension(entry->name, {"firm"}) ? "Boot file" : "Boot file (Direct)");
					break;
				case FileOperation::bootstrapFile:
					font->print(3, row++, false, "Bootstrap file");
					break;
				case FileOperation::mountNitroFS:
					font->print(3, row++, false, "Mount NitroFS");
					break;
				case FileOperation::ndsInfo:
					font->print(3, row++, false, "Show NDS file info");
					break;
				case FileOperation::trimNds:
					font->print(3, row++, false, "Trim NDS file");
					break;
				case FileOperation::restoreSave:
					font->print(3, row++, false, "Restore save");
					break;
				case FileOperation::mountImg:
					font->print(3, row++, false, "Mount as FAT image");
					break;
				case FileOperation::hexEdit:
					font->print(3, row++, false, "Open in hex editor");
					break;
				case FileOperation::showInfo:
					font->print(3, row++, false, entry->isDirectory ? "Show directory info" : "Show file info");
					break;
				case FileOperation::copySdOut:
					font->print(3, row++, false, "Copy to sd:/gm9i/out");
					break;
				case FileOperation::copyFatOut:
					font->print(3, row++, false, "Copy to fat:/gm9i/out");
					break;
				case FileOperation::calculateSHA1:
					font->print(3, row++, false, "Calculate SHA1 hash");
					break;
				case FileOperation::loadFont:
					font->print(3, row++, false, "Load font");
					break;
				case FileOperation::none:
					row++;
					break;
			}
		}

		font->print(3, ++row, false, "(<A> select, <B> cancel)");

		// Show cursor
		font->print(0, y + optionOffset, false, "->");

		font->update(false);

		// Power saving loop. Only poll the keys once per frame and sleep the CPU if there is nothing else to do
		do {
			// Print time
			font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
			font->update(true);

			scanKeys();
			pressed = keysDownRepeat();
			held = keysHeld();
			swiWaitForVBlank();
		} while (!(pressed & (KEY_UP| KEY_DOWN | KEY_A | KEY_B | KEY_L))
#ifdef SCREENSWAP
				&& !(pressed & KEY_TOUCH)
#endif
				);

		if (pressed & KEY_UP)		optionOffset -= 1;
		if (pressed & KEY_DOWN)		optionOffset += 1;

		if (optionOffset < 0) // Wrap around to bottom of list
			optionOffset = operations.size() - 1;

		if (optionOffset >= (int)operations.size()) // Wrap around to top of list
			optionOffset = 0;

		if (pressed & KEY_A) {
			switch(operations[optionOffset]) {
				case FileOperation::bootFile: {
					applaunch = true;
					font->print(3, optionOffset + y, false, "Now loading...");
					font->update(false);
					break;
				} case FileOperation::bootstrapFile: {
					char baseFile[256], savePath[PATH_MAX]; //, bootstrapConfigPath[32];
					//snprintf(bootstrapConfigPath, 32, "%s:/_nds/nds-bootstrap.ini", isDSiMode() ? "sd" : "fat");
					strncpy(baseFile, entry->name.c_str(), 255);
					*strrchr(baseFile, '.') = 0;
					snprintf(savePath, PATH_MAX, "%s%s%s.sav", path, !access("saves", F_OK) ? "saves/" : "", baseFile);
					CIniFile bootstrapConfig("/_nds/nds-bootstrap.ini");
					bootstrapConfig.SetString("NDS-BOOTSTRAP", "NDS_PATH", fullPath);
					bootstrapConfig.SetString("NDS-BOOTSTRAP", "SAV_PATH", savePath);
					bootstrapConfig.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
					bootstrapConfig.SaveIniFile("/_nds/nds-bootstrap.ini");
					// TODO Something less hacky lol
					chdir(isDSiMode()&&sdMounted ? "sd:/_nds" : "fat:/_nds");
					// TODO Read header and check for homebrew flag, based on that runNdsFile nds-bootstrap(-hb)-release
					entry->name = "nds-bootstrap-release.nds";
					applaunch = true;
					return FileOperation::bootFile;
					break;
				} case FileOperation::restoreSave: {
					ndsCardSaveRestore(entry->name.c_str());
					break;
				} case FileOperation::copySdOut: {
					if (access("sd:/gm9i", F_OK) != 0) {
						font->print(3, optionOffset + y, false, "Creating directory...");
						font->update(false);
						mkdir("sd:/gm9i", 0777);
					}
					if (access("sd:/gm9i/out", F_OK) != 0) {
						font->print(3, optionOffset + y, false, "Creating directory...");
						font->update(false);
						mkdir("sd:/gm9i/out", 0777);
					}
					char destPath[256];
					snprintf(destPath, sizeof(destPath), "sd:/gm9i/out/%s", entry->name.c_str());
					font->print(3, optionOffset + y, false, "Copying...");
					font->update(false);
					remove(destPath);
					char sourceFolder[PATH_MAX];
					getcwd(sourceFolder, PATH_MAX);
					char sourcePath[PATH_MAX];
					snprintf(sourcePath, sizeof(sourcePath), "%s%s", sourceFolder, entry->name.c_str());
					fcopy(sourcePath, destPath);
					chdir(sourceFolder);	// For after copying a folder
					break;
				} case FileOperation::copyFatOut: {
					if (access("fat:/gm9i", F_OK) != 0) {
						font->print(3, optionOffset + y, false, "Creating directory...");
						font->update(false);
						mkdir("fat:/gm9i", 0777);
					}
					if (access("fat:/gm9i/out", F_OK) != 0) {
						font->print(3, optionOffset + y, false, "Creating directory...");
						font->update(false);
						mkdir("fat:/gm9i/out", 0777);
					}
					char destPath[256];
					snprintf(destPath, sizeof(destPath), "fat:/gm9i/out/%s", entry->name.c_str());
					font->print(3, (optionOffset + y), false, "Copying...");
					font->update(false);
					remove(destPath);
					char sourceFolder[PATH_MAX];
					getcwd(sourceFolder, PATH_MAX);
					char sourcePath[PATH_MAX];
					snprintf(sourcePath, sizeof(sourcePath), "%s%s", sourceFolder, entry->name.c_str());
					fcopy(sourcePath, destPath);
					chdir(sourceFolder);	// For after copying a folder
					break;
				} case FileOperation::mountNitroFS: {
					nitroMounted = nitroFSInit(entry->name.c_str());
					if (nitroMounted) {
						chdir("nitro:/");
						nitroCurrentDrive = currentDrive;
						currentDrive = 5;
					}
					break;
				} case FileOperation::ndsInfo: {
					ndsInfo(entry->name.c_str());
					break;
				} case FileOperation::trimNds: {
					entry->size = trimNds(entry->name.c_str());
					break;
				} case FileOperation::showInfo: {
					changeFileAttribs(entry);
					break;
				} case FileOperation::mountImg: {
					imgMounted = imgMount(entry->name.c_str());
					if (imgMounted) {
						chdir("img:/");
						imgCurrentDrive = currentDrive;
						currentDrive = 6;
					}
					break;
				} case FileOperation::hexEdit: {
					hexEditor(entry->name.c_str(), currentDrive);
					break;
				} case FileOperation::loadFont: {
					delete font;
					font = new Font(entry->name.c_str());
					break;
				} case FileOperation::calculateSHA1: {
					u8 sha1[20] = {0};
					bool ret = calculateSHA1(strcat(getcwd(path, PATH_MAX), entry->name.c_str()), sha1);
					if (!ret)
						break;

					font->clear(false);
					font->print(0, 0, false, "SHA1 hash is:");
					char sha1Str[41];
					for (int i = 0; i < 20; ++i)
						sniprintf(sha1Str + i * 2, 3, "%02X", sha1[i]);
					font->print(0, 1, false, sha1Str);
					font->print(0, font->calcHeight(sha1Str) + 2, false, "(<A> to continue)");
					font->update(false);

					// Power saving loop. Only poll the keys once per frame and sleep the CPU if there is nothing else to do
					int pressed;
					do {
						// Print time
						font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
						font->update(true);

						scanKeys();
						pressed = keysDownRepeat();
						swiWaitForVBlank();

						if(keysHeld() & KEY_R && pressed & KEY_L) {
							screenshot();
						}
					} while (!(pressed & (KEY_A | KEY_Y | KEY_B | KEY_X)));
					break;
				} case FileOperation::none: {
					break;
				}
			}
			return operations[optionOffset];
		}
		if (pressed & KEY_B) {
			return FileOperation::none;
		}
#ifdef SCREENSWAP
		// Swap screens
		if (pressed & KEY_TOUCH) {
			screenSwapped = !screenSwapped;
			screenSwapped ? lcdMainOnBottom() : lcdMainOnTop();
		}
#endif

		// Make a screenshot
		if ((held & KEY_R) && (pressed & KEY_L)) {
			screenshot();
		}
	}
}

bool fileBrowse_paste(char dest[256]) {
	int pressed = 0;
	int optionOffset = 0;

	while (true) {
		font->clear(false);

		font->print(0, 0, false, "Paste clipboard here?");

		int row = OPTIONS_ENTRIES_START_ROW, maxCursors = 0;
		font->print(3, row++, false, "Copy files");
		for (auto &file : clipboard) {
			if (file.nitro)
				continue;
			maxCursors++;
			font->print(3, row++, false, "Move files");
			break;
		}
		font->print(3, ++row, false, "(<A> select, <B> cancel)");

		// Show cursor
		font->print(0, optionOffset + OPTIONS_ENTRIES_START_ROW, false, "->");

		font->update(false);

		// Power saving loop. Only poll the keys once per frame and sleep the CPU if there is nothing else to do
		do {
			// Print time
			font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
			font->update(true);

			scanKeys();
			pressed = keysDownRepeat();
			swiWaitForVBlank();
		} while (!(pressed & KEY_UP) && !(pressed & KEY_DOWN)
				&& !(pressed & KEY_A) && !(pressed & KEY_B)
#ifdef SCREENSWAP
				&& !(pressed & KEY_TOUCH)
#endif
				);

		if (pressed & KEY_UP)		optionOffset -= 1;
		if (pressed & KEY_DOWN)		optionOffset += 1;

		if (optionOffset < 0)				optionOffset = maxCursors;		// Wrap around to bottom of list
		if (optionOffset > maxCursors)		optionOffset = 0;		// Wrap around to top of list

		if (pressed & KEY_A) {
			font->print(3, optionOffset + OPTIONS_ENTRIES_START_ROW, false, optionOffset ? "Moving... " : "Copying...");
			for (auto &file : clipboard) {
				std::string destPath = dest + file.name;
				if (file.path == destPath)
					continue;	// If the source and destination for the clipped file is the same skip it

				if (optionOffset && !file.nitro ) {	 // Don't remove if from nitro
					if (currentDrive == file.drive) {
						rename(file.path.c_str(), destPath.c_str());
					} else {
						fcopy(file.path.c_str(), destPath.c_str());		// Copy file to destination, since renaming won't work
						remove(file.path.c_str());				// Delete source file after copying
					}
				} else {
					remove(destPath.c_str());
					fcopy(file.path.c_str(), destPath.c_str());
				}
			}
			clipboardUsed = true;		// Disable clipboard restore
			clipboardOn = false;	// Clear clipboard after copying or moving
			return true;
		}
		if (pressed & KEY_B) {
			return false;
		}
#ifdef SCREENSWAP
		// Swap screens
		if (pressed & KEY_TOUCH) {
			screenSwapped = !screenSwapped;
			screenSwapped ? lcdMainOnBottom() : lcdMainOnTop();
		}
#endif
	}
}

void recRemove(const char *path, std::vector<DirEntry> dirContents) {
	chdir (path);
	getDirectoryContents(dirContents);
	for (int i = 1; i < ((int)dirContents.size()); i++) {
		DirEntry &entry = dirContents[i];
		if (entry.isDirectory)
			recRemove(entry.name.c_str(), dirContents);
		if (!(FAT_getAttr(entry.name.c_str()) & ATTR_READONLY)) {
			remove(entry.name.c_str());
		}
	}
	chdir ("..");
	remove(path);
}

void fileBrowse_drawBottomScreen(DirEntry* entry) {
	font->clear(false);

	int row = -1;

	if (!isDSiMode() && isRegularDS) {
		font->print(0, row--, false, POWERTEXT_DS);
	} else if (is3DS) {
		font->print(0, row--, false, HOMETEXT);
		font->print(0, row--, false, POWERTEXT_3DS);
	} else {
		font->print(0, row--, false, POWERTEXT);
	}
	font->print(0, row--, false, clipboardOn ? "SELECT - Clear Clipboard" : "SELECT - Restore Clipboard");
	if (sdMounted || flashcardMounted) {
		font->print(0, row--, false, SCREENSHOTTEXT);
	}
	font->print(0, row--, false, "R+A - Directory options\n");
	font->printf(0, row--, false, Alignment::left, Palette::white, "Y - %s file/[+R] CREATE entry", clipboardOn ? "PASTE" : "COPY");
	font->printf(0, row--, false, Alignment::left, Palette::white, "L - %s files (with ↑↓→←)\n", entry->selected ? "DESELECT" : "SELECT");
	font->print(0, row--, false, "X - DELETE/[+R] RENAME file\n");
	font->print(0, row--, false, titleName);

	Palette pal = entry->selected ? Palette::yellow : (entry->isDirectory ? Palette::blue : Palette::gray);
	font->print(0, 0, false, entry->name, Alignment::left, pal);
	if (entry->name != "..") {
		if (entry->isDirectory) {
			font->print(0, font->calcHeight(entry->name), false, "(dir)", Alignment::left, pal);
		} else if (entry->size == 1) {
			font->printf(0, font->calcHeight(entry->name), false, Alignment::left, pal, "%i Byte", entry->size);
		} else {
			font->printf(0, font->calcHeight(entry->name), false, Alignment::left, pal, "%i Bytes", entry->size);
		}
	}
	if (clipboardOn) {
		font->print(0, 6, false, "[CLIPBOARD]");
		for (size_t i = 0; i < clipboard.size(); ++i) {
			if (i < 4) {
				font->print(0, 7 + i, false, clipboard[i].name, Alignment::left, clipboard[i].folder ? Palette::blue : Palette::gray);
			} else {
				font->printf(0, 7 + i, false, Alignment::left, Palette::gray, "%d more files...", clipboard.size() - 4);
				break;
			}
		}
	}

	font->update(false);
}

std::string browseForFile (void) {
	int pressed = 0;
	int held = 0;
	int screenOffset = 0;
	int fileOffset = 0;
	std::vector<DirEntry> dirContents;

	getDirectoryContents (dirContents);

	while (true) {
		DirEntry* entry = &dirContents[fileOffset];

		fileBrowse_drawBottomScreen(entry);
		showDirectoryContents(dirContents, fileOffset, screenOffset);

		stored_SCFG_MC = REG_SCFG_MC;

		// Power saving loop. Only poll the keys once per frame and sleep the CPU if there is nothing else to do
		do {
			// Print time
			font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
			font->update(true);

			scanKeys();
			pressed = keysDownRepeat();
			held = keysHeld();
			swiWaitForVBlank();

			if (REG_SCFG_MC != stored_SCFG_MC) {
				break;
			}

			if ((held & KEY_R) && (pressed & KEY_L)) {
				break;
			}
		} while (!pressed);

		if (isDSiMode() && !pressed && currentDrive == 1 && REG_SCFG_MC == 0x11 && flashcardMounted) {
			flashcardUnmount();
			screenMode = 0;
			return "null";
		}

		if (pressed & KEY_UP) {
			fileOffset--;
			if(fileOffset < 0)
				fileOffset = dirContents.size() - 1;
		} else if (pressed & KEY_DOWN) {
			fileOffset++;
			if(fileOffset > (int)dirContents.size() - 1)
				fileOffset = 0;
		} else if (pressed & KEY_LEFT) {
			fileOffset -= ENTRY_PAGE_LENGTH;
			if(fileOffset < 0)
				fileOffset = 0;
		} else if (pressed & KEY_RIGHT) {
			fileOffset += ENTRY_PAGE_LENGTH;
			if(fileOffset > (int)dirContents.size() - 1)
				fileOffset = dirContents.size() - 1;
		}


		// Scroll screen if needed
		if (fileOffset < screenOffset)	{
			screenOffset = fileOffset;
		}
		if (fileOffset > screenOffset + ENTRIES_PER_SCREEN - 1) {
			screenOffset = fileOffset - ENTRIES_PER_SCREEN + 1;
		}

		getcwd(path, PATH_MAX);

		if ((!(held & KEY_R) && (pressed & KEY_A))
		|| (!entry->isDirectory && (held & KEY_R) && (pressed & KEY_A))) {
			if (entry->name == ".." && strcmp(path, getDrivePath()) == 0)
			{
				screenMode = 0;
				return "null";
			} else if (entry->isDirectory) {
				font->printf(0, fileOffset - screenOffset + ENTRIES_START_ROW, true, Alignment::left, Palette::white, "%-*s", SCREEN_COLS - 5, "Entering directory");
				font->update(true);
				// Enter selected directory
				chdir (entry->name.c_str());
				getDirectoryContents(dirContents);
				screenOffset = 0;
				fileOffset = 0;
			} else {
				FileOperation getOp = fileBrowse_A(entry, path);
				if(getOp == FileOperation::bootFile) {
					// Return the chosen file
					return entry->name;
				} else if (getOp == FileOperation::copySdOut
						|| getOp == FileOperation::copyFatOut
						|| (getOp == FileOperation::mountNitroFS && nitroMounted)
						|| (getOp == FileOperation::mountImg && imgMounted)) {
					getDirectoryContents(dirContents); // Refresh directory listing
					if ((getOp == FileOperation::mountNitroFS && nitroMounted)
					 || (getOp == FileOperation::mountImg && imgMounted)) {
						screenOffset = 0;
						fileOffset = 0;
					}
				} else if(getOp == FileOperation::showInfo) {
					for (int i = 0; i < 15; i++) swiWaitForVBlank();
				}
			}
		}

		// Directory options
		if (entry->isDirectory && (held & KEY_R) && (pressed & KEY_A)) {
			if (entry->name == "..") {
				screenMode = 0;
				return "null";
			} else {
				FileOperation getOp = fileBrowse_A(entry, path);
				if (getOp == FileOperation::copySdOut || getOp == FileOperation::copyFatOut) {
					getDirectoryContents (dirContents);		// Refresh directory listing
				} else if (getOp == FileOperation::showInfo) {
					for (int i = 0; i < 15; i++) swiWaitForVBlank();
				}
			}
		}

		if (pressed & KEY_B) {
			if (strcmp(path, getDrivePath()) == 0) {
				screenMode = 0;
				return "null";
			}
			// Go up a directory
			chdir ("..");
			getDirectoryContents (dirContents);
			screenOffset = 0;
			fileOffset = 0;
		}

		// Rename file/folder
		if ((held & KEY_R) && (pressed & KEY_X) && (entry->name != ".." && strncmp(path, "nitro:/", 7) != 0)) {
			// Clear time
			font->print(-1, 0, true, "     ", Alignment::right, Palette::blackGreen);
			font->update(true);

			pressed = 0;
			consoleDemoInit();
			Keyboard *kbd = keyboardDemoInit();
			char newName[256];
			kbd->OnKeyPressed = OnKeyPressed;

			keyboardShow();
			iprintf("Rename to:\n");
			fgets(newName, 256, stdin);
			newName[strlen(newName)-1] = 0;
			keyboardHide();

			videoSetModeSub(MODE_5_2D);
			bgShow(bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 3, 0));

			if (newName[0] != '\0') {
				// Check for unsupported characters
				for (int i = 0; i < (int)sizeof(newName); i++) {
					if (newName[i] == '>'
					|| newName[i] == '<'
					|| newName[i] == ':'
					|| newName[i] == '"'
					|| newName[i] == '/'
					|| newName[i] == '\x5C'
					|| newName[i] == '|'
					|| newName[i] == '?'
					|| newName[i] == '*')
					{
						newName[i] = '_';	// Remove unsupported character
					}
				}
				if (rename(entry->name.c_str(), newName) == 0) {
					getDirectoryContents(dirContents);
				}
			}
		}

		// Delete action
		if ((pressed & KEY_X) && (entry->name != ".." && strncmp(path, "nitro:/", 7) != 0)) {
			font->clear(false);
			int selections = std::count_if(dirContents.begin(), dirContents.end(), [](const DirEntry &x){ return x.selected; });
			if (entry->selected && selections > 1) {
				font->printf(0, 0, false, Alignment::left, Palette::white, "Delete %d paths?", selections);
				for (uint i = 0, printed = 0; i < dirContents.size() && printed < 5; i++) {
					if (dirContents[i].selected) {
						font->printf(0, printed + 2, false, Alignment::left, Palette::red, "- %s", dirContents[i].name.c_str());
						printed++;
					}
				}
				if(selections > 5)
					font->printf(0, 7, false, Alignment::left, Palette::red, "- and %d more...", selections - 5);
			} else {
				font->printf(0, 0, false, Alignment::left, Palette::white, "Delete \"%s\"?", entry->name.c_str());
			}
			font->print(0, (!entry->selected || selections == 1) ? 2 : (selections > 5 ? 9 : selections + 3), false, "(<A> yes, <B> no)");
			font->update(false);

			while (true) {
				// Print time
				font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
				font->update(true);

				scanKeys();
				pressed = keysDownRepeat();
				swiWaitForVBlank();
				if (pressed & KEY_A) {
					if (entry->selected) {
						font->clear(false);
						font->print(0, 0, false, "Deleting files, please wait...");
						font->update(false);
						struct stat st;
						for (auto &item : dirContents) {
							if(item.selected) {
								if (FAT_getAttr(item.name.c_str()) & ATTR_READONLY)
									continue;
								stat(item.name.c_str(), &st);
								if (st.st_mode & S_IFDIR)
									recRemove(item.name.c_str(), dirContents);
								else
									remove(item.name.c_str());
							}
						}
						fileOffset = 0;
					} else if (FAT_getAttr(entry->name.c_str()) & ATTR_READONLY) {
						font->clear(false);
						font->print(0, 0, false, "Failed deleting:");
						font->print(0, 1, false, entry->name);
						font->print(0, 3, false, "(<A> to continue)");
						pressed = 0;

						while (!(pressed & KEY_A)) {
							// Print time
							font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
							font->update(true);

							scanKeys();
							pressed = keysDown();
							swiWaitForVBlank();
						}
						for (int i = 0; i < 15; i++) swiWaitForVBlank();
					} else {
						if (entry->isDirectory) {
							font->clear(false);
							font->print(0, 0, false, "Deleting folder, please wait...");
							font->update(false);
							recRemove(entry->name.c_str(), dirContents);
						} else {
							font->clear(false);
							font->print(0, 0, false, "Deleting folder, please wait...");
							font->update(false);
							remove(entry->name.c_str());
						}
						fileOffset--;
					}
					getDirectoryContents (dirContents);
					pressed = 0;
					break;
				}
				if (pressed & KEY_B) {
					pressed = 0;
					break;
				}
			}
		}

		// Create new folder
		if ((held & KEY_R) && (pressed & KEY_Y) && (strncmp(path, "nitro:/", 7) != 0)) {
			// Clear time
			font->print(-1, 0, true, "     ", Alignment::right, Palette::blackGreen);
			font->update(true);

			pressed = 0;
			consoleDemoInit();
			Keyboard *kbd = keyboardDemoInit();
			char newName[256];
			kbd->OnKeyPressed = OnKeyPressed;

			keyboardShow();
			iprintf("Name for new folder:\n");
			fgets(newName, 256, stdin);
			newName[strlen(newName)-1] = 0;
			keyboardHide();

			videoSetModeSub(MODE_5_2D);
			bgShow(bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 3, 0));

			if (newName[0] != '\0') {
				// Check for unsupported characters
				for (int i = 0; i < (int)sizeof(newName); i++) {
					if (newName[i] == '>'
					|| newName[i] == '<'
					|| newName[i] == ':'
					|| newName[i] == '"'
					|| newName[i] == '/'
					|| newName[i] == '\x5C'
					|| newName[i] == '|'
					|| newName[i] == '?'
					|| newName[i] == '*')
					{
						newName[i] = '_';	// Remove unsupported character
					}
				}
				if (mkdir(newName, 0777) == 0) {
					getDirectoryContents (dirContents);
				}
			}
		}

		// Add to selection
		if ((pressed & KEY_L && !(held & KEY_R)) && entry->name != "..") {
			bool select = !entry->selected;
			entry->selected = select;
			while(held & KEY_L) {
				do {
					// Print time
					font->print(-1, 0, true, RetTime(), Alignment::right, Palette::blackGreen);
					font->update(true);

					scanKeys();
					pressed = keysDownRepeat();
					held = keysHeld();
					swiWaitForVBlank();
				} while ((held & KEY_L) && !(pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)));

				if(pressed & (KEY_UP | KEY_DOWN)) {
					if (pressed & KEY_UP) {
						fileOffset--;
						if(fileOffset < 0) {
							fileOffset = dirContents.size() - 1;
						} else {
							entry = &dirContents[fileOffset];
							if(entry->name != "..")
								entry->selected = select;
						}
					} else if (pressed & KEY_DOWN) {
						fileOffset++;
						if(fileOffset > (int)dirContents.size() - 1) {
							fileOffset = 0;
						} else {
							entry = &dirContents[fileOffset];
							if(entry->name != "..")
								entry->selected = select;
						}
					}

					// Scroll screen if needed
					if (fileOffset < screenOffset)	{
						screenOffset = fileOffset;
					} else if (fileOffset > screenOffset + ENTRIES_PER_SCREEN - 1) {
						screenOffset = fileOffset - ENTRIES_PER_SCREEN + 1;
					}
				}
				
				if(pressed & KEY_LEFT) {
					for(auto &item : dirContents) {
						if(item.name != "..")
							item.selected = false;
					}
				} else if(pressed & KEY_RIGHT) {
					for(auto &item : dirContents) {
						if(item.name != "..")
							item.selected = true;
					}
				}

				fileBrowse_drawBottomScreen(entry);
				showDirectoryContents(dirContents, fileOffset, screenOffset);
			}
		}

		if (pressed & KEY_Y) {
			// Copy
			if (!clipboardOn) {
				if (entry->name != "..") {
					clipboardOn = true;
					clipboardUsed = false;
					clipboard.clear();
					if (entry->selected) {
						for (auto &item : dirContents) {
							if(item.selected) {
								clipboard.emplace_back(path + item.name, item.name, item.isDirectory, currentDrive, !strncmp(path, "nitro:/", 7));
								item.selected = false;
							}
						}
					} else {
						clipboard.emplace_back(path + entry->name, entry->name, entry->isDirectory, currentDrive, !strncmp(path, "nitro:/", 7));
					}
				}
			// Paste
			} else if (strncmp(path, "nitro:/", 7) != 0 && fileBrowse_paste(path)) {
				getDirectoryContents (dirContents);
			}
		}

		if ((pressed & KEY_SELECT) && !clipboardUsed) {
			clipboardOn = !clipboardOn;
		}

#ifdef SCREENSWAP
		// Swap screens
		if (pressed & KEY_TOUCH) {
			screenSwapped = !screenSwapped;
			screenSwapped ? lcdMainOnBottom() : lcdMainOnTop();
		}
#endif

		// Make a screenshot
		if ((held & KEY_R) && (pressed & KEY_L)) {
			if(screenshot())
				getDirectoryContents(dirContents);
		}
	}
}
