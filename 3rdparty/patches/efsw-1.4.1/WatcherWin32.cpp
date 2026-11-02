#include <efsw/Debug.hpp>
#include <efsw/FileWatcherWin32.hpp>
#include <efsw/String.hpp>
#include <efsw/WatcherWin32.hpp>

#if EFSW_PLATFORM == EFSW_PLATFORM_WIN32

#include <algorithm>

namespace efsw {

struct EFSW_FILE_NOTIFY_EXTENDED_INFORMATION_EX {
	DWORD NextEntryOffset;
	DWORD Action;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastModificationTime;
	LARGE_INTEGER LastChangeTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER AllocatedLength;
	LARGE_INTEGER FileSize;
	DWORD FileAttributes;
	DWORD ReparsePointTag;
	LARGE_INTEGER FileId;
	LARGE_INTEGER ParentFileId;
	DWORD FileNameLength;
	WCHAR FileName[1];
};

typedef EFSW_FILE_NOTIFY_EXTENDED_INFORMATION_EX* EFSW_PFILE_NOTIFY_EXTENDED_INFORMATION_EX;

typedef BOOL( WINAPI* EFSW_LPREADDIRECTORYCHANGESEXW )( HANDLE hDirectory, LPVOID lpBuffer,
												   DWORD nBufferLength, BOOL bWatchSubtree,
												   DWORD dwNotifyFilter, LPDWORD lpBytesReturned,
												   LPOVERLAPPED lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
												   DWORD ReadDirectoryNotifyInformationClass );

static EFSW_LPREADDIRECTORYCHANGESEXW pReadDirectoryChangesExW = NULL;

#define EFSW_ReadDirectoryNotifyExtendedInformation 2

static void initReadDirectoryChangesEx() {
	static bool hasInit = false;
	if ( !hasInit ) {
		hasInit = true;

		HMODULE hModule = GetModuleHandleW( L"Kernel32.dll" );
		if ( !hModule )
			return;

		pReadDirectoryChangesExW =
			(EFSW_LPREADDIRECTORYCHANGESEXW)GetProcAddress( hModule, "ReadDirectoryChangesExW" );
	}
}

void WatchCallbackOld( WatcherWin32* pWatch ) {
	PFILE_NOTIFY_INFORMATION pNotify;
	size_t offset = 0;
	do {
		bool skip = false;

		pNotify = (PFILE_NOTIFY_INFORMATION)&pWatch->Buffer[offset];
		offset += pNotify->NextEntryOffset;
		int count =
			WideCharToMultiByte( CP_UTF8, 0, pNotify->FileName,
								 pNotify->FileNameLength / sizeof( WCHAR ), NULL, 0, NULL, NULL );
		if ( count == 0 )
			continue;

		std::string nfile( count, '\0' );

		count = WideCharToMultiByte( CP_UTF8, 0, pNotify->FileName,
									 pNotify->FileNameLength / sizeof( WCHAR ), &nfile[0], count,
									 NULL, NULL );

		if ( FILE_ACTION_MODIFIED == pNotify->Action ) {
			FileInfo fifile( std::string( pWatch->DirName ) + nfile );

			if ( pWatch->LastModifiedEvent.file.ModificationTime == fifile.ModificationTime &&
				 pWatch->LastModifiedEvent.file.Size == fifile.Size &&
				 pWatch->LastModifiedEvent.fileName == nfile ) {
				skip = true;
			}

			pWatch->LastModifiedEvent.fileName = nfile;
			pWatch->LastModifiedEvent.file = fifile;
		}

		if ( !skip ) {
			pWatch->Watch->handleAction( pWatch, nfile, pNotify->Action );
		}
	} while ( pNotify->NextEntryOffset != 0 );
}

void WatchCallbackEx( WatcherWin32* pWatch ) {
	EFSW_PFILE_NOTIFY_EXTENDED_INFORMATION_EX pNotify;
	size_t offset = 0;
	do {
		bool skip = false;

		pNotify = (EFSW_PFILE_NOTIFY_EXTENDED_INFORMATION_EX)&pWatch->Buffer[offset];
		offset += pNotify->NextEntryOffset;
		int count =
			WideCharToMultiByte( CP_UTF8, 0, pNotify->FileName,
								 pNotify->FileNameLength / sizeof( WCHAR ), NULL, 0, NULL, NULL );
		if ( count == 0 )
			continue;

		std::string nfile( count, '\0' );

		count = WideCharToMultiByte( CP_UTF8, 0, pNotify->FileName,
									 pNotify->FileNameLength / sizeof( WCHAR ), &nfile[0], count,
									 NULL, NULL );

		if ( FILE_ACTION_MODIFIED == pNotify->Action ) {
			FileInfo fifile( std::string( pWatch->DirName ) + nfile );

			if ( pWatch->LastModifiedEvent.file.ModificationTime == fifile.ModificationTime &&
				 pWatch->LastModifiedEvent.file.Size == fifile.Size &&
				 pWatch->LastModifiedEvent.fileName == nfile ) {
				skip = true;
			}

			pWatch->LastModifiedEvent.fileName = nfile;
			pWatch->LastModifiedEvent.file = fifile;
		} else if ( FILE_ACTION_RENAMED_OLD_NAME == pNotify->Action ) {
			pWatch->OldFiles.emplace_back( nfile, pNotify->FileId );
			skip = true;
		} else if ( FILE_ACTION_RENAMED_NEW_NAME == pNotify->Action ) {
			std::string oldFile;
			LARGE_INTEGER oldFileId{};

			for ( auto it = pWatch->OldFiles.begin(); it != pWatch->OldFiles.end(); ++it ) {
				if ( it->second.QuadPart == pNotify->FileId.QuadPart ) {
					oldFile = it->first;
					oldFileId = it->second;
					it = pWatch->OldFiles.erase( it );
					break;
				}
			}

			if ( oldFile.empty() ) {
				pWatch->Watch->handleAction( pWatch, nfile, FILE_ACTION_ADDED );
				skip = true;
			} else {
				pWatch->Watch->handleAction( pWatch, oldFile, FILE_ACTION_RENAMED_OLD_NAME );
			}
		}

		if ( !skip ) {
			pWatch->Watch->handleAction( pWatch, nfile, pNotify->Action );
		}
	} while ( pNotify->NextEntryOffset != 0 );
}

/// Unpacks events and passes them to a user defined callback.
void CALLBACK WatchCallback( DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped ) {
	if ( NULL == lpOverlapped ) {
		return;
	}

	WatcherStructWin32* tWatch = (WatcherStructWin32*)lpOverlapped;
	WatcherWin32* pWatch = tWatch->Watch;

	if ( pWatch->StopNow ) {
		return;
	}

	// klogg patch: re-arm exactly once per completion. The upstream code
	// armed twice for zero-byte completions (buffer overflow/lost events):
	// once in the early branch and once here, leaving multiple IRPs pending
	// on the same OVERLAPPED. A later DestroyWatch could then not wait for
	// all of them, and the surviving IRP completed into freed heap.
	if ( dwNumberOfBytesTransfered != 0 ) {
		// Fork watch depending on the Windows API supported
		if ( pWatch->Extended ) {
			WatchCallbackEx( pWatch );
		}
		else {
			WatchCallbackOld( pWatch );
		}
	}

	RefreshWatch( tWatch );
}

/// Refreshes the directory monitoring.
RefreshResult RefreshWatch( WatcherStructWin32* pWatch ) {
	initReadDirectoryChangesEx();

	bool bRet = false;
	RefreshResult ret = RefreshResult::Failed;
	pWatch->Watch->Extended = false;

	if ( pReadDirectoryChangesExW ) {
		bRet = pReadDirectoryChangesExW( pWatch->Watch->DirHandle, pWatch->Watch->Buffer.data(),
									  (DWORD)pWatch->Watch->Buffer.size(), pWatch->Watch->Recursive,
									  pWatch->Watch->NotifyFilter, NULL, &pWatch->Overlapped,
										 NULL, EFSW_ReadDirectoryNotifyExtendedInformation ) != 0;
		if ( bRet ) {
			ret = RefreshResult::SucessEx;
			pWatch->Watch->Extended = true;
		}
	}

	if ( !bRet ) {
		bRet = ReadDirectoryChangesW( pWatch->Watch->DirHandle, pWatch->Watch->Buffer.data(),
									  (DWORD)pWatch->Watch->Buffer.size(), pWatch->Watch->Recursive,
									  pWatch->Watch->NotifyFilter, NULL, &pWatch->Overlapped,
									  NULL ) != 0;

		if ( bRet )
			ret = RefreshResult::Success;
	}

	if ( !bRet ) {
		std::string error = std::to_string( GetLastError() );
		Errors::Log::createLastError( Errors::WatcherFailed, error );
	}

	return ret;
}

/// Stops monitoring a directory.
void DestroyWatch( WatcherStructWin32* pWatch ) {
	if ( pWatch ) {
		WatcherWin32* tWatch = pWatch->Watch;
		tWatch->StopNow = true;
		// klogg patch v2: CancelIoEx only requests cancellation; the IRP still
		// completes asynchronously (possibly much later with filter drivers
		// in the I/O stack) and the kernel writes the OVERLAPPED and the
		// notify buffer at completion time, so freeing them here corrupts
		// the heap. Defer the delete until the completion packet is reaped
		// from the IOCP by the watcher thread.
		//
		// v2: drop the immediate-free fast path entirely. A watch whose IRP
		// completed but whose packet is still queued reports ERROR_NOT_FOUND
		// from CancelIoEx; freeing it immediately returned the struct to the
		// heap, and the queued packet could then be misattributed to a *new*
		// watch that reused the same address, arming a second IRP on it — the
		// graveyard reaps only the first packet, so the surviving IRP completed
		// into freed heap. Deferring unconditionally keeps the struct address
		// occupied until its packet is reaped, so a stale packet always lands
		// in the graveyard branch and never matches a live watch.
		// Watches whose last completion failed (no re-arm, no IRP, no packet)
		// leak their graveyard entry once; that is bounded and beats a
		// use-after-free.
		CancelIoEx( tWatch->DirHandle, &pWatch->Overlapped );
		CloseHandle( tWatch->DirHandle );
		if ( NULL != tWatch->Watch ) {
			static_cast<FileWatcherWin32*>( tWatch->Watch )->deferWatchDestruction( pWatch );
		}
		else {
			efSAFE_DELETE_ARRAY( tWatch->DirName );
			efSAFE_DELETE( tWatch );
			efSAFE_DELETE( pWatch );
		}
	}
}

/// Starts monitoring a directory.
WatcherStructWin32* CreateWatch( LPCWSTR szDirectory, bool recursive,
								 DWORD bufferSize, DWORD notifyFilter, HANDLE iocp ) {
	WatcherStructWin32* tWatch = new WatcherStructWin32();
	WatcherWin32* pWatch = new WatcherWin32(bufferSize);
	if (tWatch)
		tWatch->Watch = pWatch;

	pWatch->DirHandle = CreateFileW(
		szDirectory, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL );

	if ( pWatch->DirHandle != INVALID_HANDLE_VALUE &&
		 CreateIoCompletionPort( pWatch->DirHandle, iocp, 0, 1 ) ) {
		pWatch->NotifyFilter = notifyFilter;
		pWatch->Recursive = recursive;

		if ( RefreshResult::Failed != RefreshWatch( tWatch ) ) {
			return tWatch;
		}
	}

	CloseHandle( pWatch->DirHandle );
	efSAFE_DELETE( pWatch->Watch );
	efSAFE_DELETE( tWatch );
	return NULL;
}

} // namespace efsw

#endif
