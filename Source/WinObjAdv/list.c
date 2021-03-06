/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2015
*
*  TITLE:       LIST.C
*
*  VERSION:     1.00
*
*  DATE:        18 Feb 2015
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/
#include "global.h"

#pragma comment(lib, "ntdll.lib")

/*
* GetNextSub
*
* Purpose:
*
* Returns next subitem in object full pathname.
*
*/
LPWSTR GetNextSub(
	LPWSTR ObjectFullPathName,
	LPWSTR Sub
	)
{
	size_t i;

	for (i = 0; (*ObjectFullPathName != 0) && (*ObjectFullPathName != '\\') && (i < MAX_PATH); i++, ObjectFullPathName++)
		Sub[i] = *ObjectFullPathName;
	Sub[i] = 0;

	if (*ObjectFullPathName == '\\')
		ObjectFullPathName++;

	return ObjectFullPathName;
}

/*
* ListToObject
*
* Purpose:
*
* Select and focus list view item by given object name.
*
*/
VOID ListToObject(
	_In_ LPWSTR ObjectName
	)
{
	HTREEITEM	lastfound, item;
	WCHAR		object[MAX_PATH + 1], sobject[MAX_PATH + 1];
	int			i, s;
	LVITEMW		lvitem;
	TVITEMEXW	ritem;
	BOOL		currentfound = FALSE;

	if (ObjectName == NULL)
		return;

	if (*ObjectName != '\\')
		return;
	ObjectName++;
	item = TreeView_GetRoot(ObjectTree);
	lastfound = item;

	while ((item != NULL) && (*ObjectName != 0)) {
		item = TreeView_GetChild(ObjectTree, item);
		RtlSecureZeroMemory(object, sizeof(object));
		ObjectName = GetNextSub(ObjectName, object);
		currentfound = FALSE;

		do {
			RtlSecureZeroMemory(&ritem, sizeof(ritem));
			RtlSecureZeroMemory(&sobject, sizeof(sobject));
			ritem.mask = TVIF_TEXT;
			ritem.hItem = item;
			ritem.cchTextMax = MAX_PATH;
			ritem.pszText = sobject;

			if (!TreeView_GetItem(ObjectTree, &ritem))
				break;

			if (_strcmpiW(sobject, object) == 0) {
				if (item)
					lastfound = item;
				break;
			}

			item = TreeView_GetNextSibling(ObjectTree, item);
		} while (item != NULL);
	}

	TreeView_SelectItem(ObjectTree, lastfound);

	if (currentfound) // final target was a subdir
		return;

	for (i = 0; i < MAXINT; i++) {
		RtlSecureZeroMemory(&lvitem, sizeof(lvitem));
		RtlSecureZeroMemory(&sobject, sizeof(sobject));
		lvitem.mask = LVIF_TEXT;
		lvitem.iItem = i;
		lvitem.cchTextMax = MAX_PATH;
		lvitem.pszText = sobject;
		if (!ListView_GetItem(ObjectList, &lvitem))
			break;

		if (_strcmpiW(sobject, object) == 0) {
			s = ListView_GetSelectionMark(ObjectList);
			lvitem.mask = LVIF_STATE;
			lvitem.stateMask = LVIS_SELECTED | LVIS_FOCUSED;

			if (s >= 0) {
				lvitem.iItem = s;
				lvitem.state = 0;
				ListView_SetItem(ObjectList, &lvitem);
			}

			lvitem.iItem = i;
			lvitem.state = LVIS_SELECTED | LVIS_FOCUSED;
			ListView_SetItem(ObjectList, &lvitem);
			ListView_EnsureVisible(ObjectList, i, FALSE);
			SetFocus(ObjectList);
			return;
		}
	}
}

/*
* AddTreeViewItem
*
* Purpose:
*
* Add item to the tree view.
*
*/
HTREEITEM AddTreeViewItem(
	_In_ LPWSTR ItemName, 
	_In_opt_ HTREEITEM Root
	)
{
	TVINSERTSTRUCTW	item;

	RtlSecureZeroMemory(&item, sizeof(item));
	item.hParent = Root;
	item.item.mask = TVIF_TEXT | TVIF_SELECTEDIMAGE;
	if (Root == NULL) {
		item.item.mask |= TVIF_STATE;
		item.item.state = TVIS_EXPANDED;
		item.item.stateMask = TVIS_EXPANDED;
	}
	item.item.iSelectedImage = 1;
	item.item.pszText = ItemName;

	return TreeView_InsertItem(ObjectTree, &item);
}

/*
* ListObjectDirectoryTree
*
* Purpose:
*
* List given directory to the treeview.
*
*/
VOID ListObjectDirectoryTree(
	_In_ LPWSTR SubDirName, 
	_In_opt_ HANDLE RootHandle,
	_In_opt_ HTREEITEM ViewRootHandle
	)
{
	OBJECT_ATTRIBUTES	objattr;
	UNICODE_STRING		objname;
	HANDLE				hDirectory = NULL;
	NTSTATUS			status;
	ULONG				ctx, rlen;
	BOOL				cond = TRUE;

	POBJECT_DIRECTORY_INFORMATION	objinf;

	ViewRootHandle = AddTreeViewItem(SubDirName, ViewRootHandle);
	RtlSecureZeroMemory(&objname, sizeof(objname));
	RtlInitUnicodeString(&objname, SubDirName);
	InitializeObjectAttributes(&objattr, &objname, OBJ_CASE_INSENSITIVE, RootHandle, NULL);
	status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objattr);
	if (!NT_SUCCESS(status))
		return;

	ctx = 0;
	do {
		rlen = 0;
		status = NtQueryDirectoryObject(hDirectory, NULL, 0, TRUE, FALSE, &ctx, &rlen);
		if (status != STATUS_BUFFER_TOO_SMALL)
			break;

		objinf = HeapAlloc(GetProcessHeap(), 0, rlen);
		if (objinf == NULL)
			break;

		RtlSecureZeroMemory(objinf, rlen);
		status = NtQueryDirectoryObject(hDirectory, objinf, rlen, TRUE, FALSE, &ctx, &rlen);
		if (!NT_SUCCESS(status)) {
			HeapFree(GetProcessHeap(), 0, objinf);
			break;
		}

		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_DIRECTORY]) == 0) {
			ListObjectDirectoryTree(objinf->Name.Buffer, hDirectory, ViewRootHandle);
		};

		HeapFree(GetProcessHeap(), 0, objinf);
	} while (cond);

	if (hDirectory != NULL)
		NtClose(hDirectory);
}

/*
* AddListViewItem
*
* Purpose:
*
* Add item to the object listview.
*
*/
VOID AddListViewItem(
	_In_ HANDLE hObjectRootDirectory,
	_In_ POBJECT_DIRECTORY_INFORMATION objinf,
	_In_ PENUM_PARAMS lpEnumParams
	)
{
	LVITEMW				lvitem;
	INT					index;
	BOOL				bFound = FALSE;
	WCHAR				szBuffer[MAX_PATH + 1];

	if (!objinf) return;

	RtlSecureZeroMemory(&lvitem, sizeof(lvitem));
	lvitem.mask = LVIF_TEXT | LVIF_IMAGE;
	lvitem.iSubItem = 0;
	lvitem.pszText = objinf->Name.Buffer;
	lvitem.iItem = MAXINT;
	lvitem.iImage = supGetObjectIndexByTypeName(objinf->TypeName.Buffer);
	index = ListView_InsertItem(ObjectList, &lvitem);

	lvitem.mask = LVIF_TEXT;
	lvitem.iSubItem = 1;
	lvitem.pszText = objinf->TypeName.Buffer;
	lvitem.iItem = index;
	ListView_SetItem(ObjectList, &lvitem);

	if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_SYMLINK]) == 0) {
		RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
		if (supQueryLinkTarget(hObjectRootDirectory, &objinf->Name, szBuffer, MAX_PATH * sizeof(WCHAR)))
			bFound = TRUE;
	}
	if (bFound != TRUE) {
		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_SECTION]) == 0) {
			RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
			if (supQuerySectionFileInfo(hObjectRootDirectory, &objinf->Name, szBuffer, MAX_PATH))
				bFound = TRUE;
		}
	}
	if (bFound != TRUE) {
		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_DRIVER]) == 0)
		{
			RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
			if (
				supQueryDriverDescription(
				objinf->Name.Buffer,
				lpEnumParams->scmSnapshot,
				lpEnumParams->scmNumberOfEntries,
				szBuffer, MAX_PATH)
				)
				bFound = TRUE;
		}
	}

	if (bFound != TRUE) {
		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_DEVICE]) == 0)
		{
			RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
			if (
				supQueryDeviceDescription(
				objinf->Name.Buffer,
				lpEnumParams->sapiDB,
				szBuffer, MAX_PATH)
				)
				bFound = TRUE;
		}
	}
	if (bFound != TRUE) {
		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_WINSTATION]) == 0) {
			RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
			bFound = supQueryWinstationDescription(objinf->Name.Buffer, szBuffer, MAX_PATH);
		}
	}
	if (bFound != TRUE) {
		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_TYPE]) == 0) {
			RtlSecureZeroMemory(&szBuffer, sizeof(szBuffer));
			bFound = supQueryTypeInfo(objinf->Name.Buffer, szBuffer, MAX_PATH);
		}
	}

	//finally add information if exists
	if (bFound != FALSE) {
		lvitem.mask = LVIF_TEXT;
		lvitem.iSubItem = 2;
		lvitem.pszText = szBuffer;
		lvitem.iItem = index;
		ListView_SetItem(ObjectList, &lvitem);
	}
}

/*
* ListObjectsInDirectory
*
* Purpose:
*
* List given directory to the listview.
*
*/
VOID ListObjectsInDirectory(
	_In_ PENUM_PARAMS lpEnumParams
	)
{
	OBJECT_ATTRIBUTES	objattr;
	UNICODE_STRING		objname;
	HANDLE				hDirectory = NULL;
	NTSTATUS			status;
	ULONG				ctx, rlen;
	BOOL				cond = TRUE;

	POBJECT_DIRECTORY_INFORMATION	objinf;

	if (lpEnumParams == NULL)
		return;

	ListView_DeleteAllItems(ObjectList);
	RtlSecureZeroMemory(&objname, sizeof(objname));
	RtlInitUnicodeString(&objname, lpEnumParams->lpSubDirName);
	InitializeObjectAttributes(&objattr, &objname, OBJ_CASE_INSENSITIVE, NULL, NULL);
	status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objattr);
	if (!NT_SUCCESS(status))
		return;

	ctx = 0;
	do {
		rlen = 0;
		status = NtQueryDirectoryObject(hDirectory, NULL, 0, TRUE, FALSE, &ctx, &rlen);
		if (status != STATUS_BUFFER_TOO_SMALL)
			break;

		objinf = HeapAlloc(GetProcessHeap(), 0, rlen);
		if (objinf == NULL)
			break;

		RtlSecureZeroMemory(objinf, rlen);
		status = NtQueryDirectoryObject(hDirectory, objinf, rlen, TRUE, FALSE, &ctx, &rlen);
		if (!NT_SUCCESS(status)) {
			HeapFree(GetProcessHeap(), 0, objinf);
			break;
		}

		AddListViewItem(hDirectory, objinf, lpEnumParams);

		HeapFree(GetProcessHeap(), 0, objinf);
	} while (cond);

	if (hDirectory != NULL) {
		NtClose(hDirectory);
	}
}

/*
* FindObject
*
* Purpose:
*
* Find object by given name in object directory.
*
*/
VOID FindObject(
	_In_ LPWSTR DirName,
	_In_opt_ LPWSTR NameSubstring,
	_In_opt_ LPWSTR TypeName,
	_In_ PFO_LIST_ITEM *List
	)
{
	OBJECT_ATTRIBUTES	objattr;
	UNICODE_STRING		objname;
	HANDLE				hDirectory = NULL;
	NTSTATUS			status;
	ULONG				ctx, rlen;
	size_t				sdlen;
	BOOL				cond = TRUE;
	PFO_LIST_ITEM		tmp;
	LPWSTR				newdir;

	POBJECT_DIRECTORY_INFORMATION	objinf;
	RtlSecureZeroMemory(&objname, sizeof(objname));
	RtlInitUnicodeString(&objname, DirName);
	sdlen = _strlenW(DirName);
	InitializeObjectAttributes(&objattr, &objname, OBJ_CASE_INSENSITIVE, NULL, NULL);
	status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objattr);
	if (!NT_SUCCESS(status))
		return;

	ctx = 0;
	do {
		rlen = 0;
		status = NtQueryDirectoryObject(hDirectory, NULL, 0, TRUE, FALSE, &ctx, &rlen);
		if (status != STATUS_BUFFER_TOO_SMALL)
			break;

		objinf = HeapAlloc(GetProcessHeap(), 0, rlen);
		if (objinf == NULL)
			break;

		RtlSecureZeroMemory(objinf, rlen);
		status = NtQueryDirectoryObject(hDirectory, objinf, rlen, TRUE, FALSE, &ctx, &rlen);
		if (!NT_SUCCESS(status)) {
			HeapFree(GetProcessHeap(), 0, objinf);
			break;
		}

		if ((_strstriW(objinf->Name.Buffer, NameSubstring) != 0) || (NameSubstring == NULL))
			if ((_strcmpiW(objinf->TypeName.Buffer, TypeName) == 0) || (TypeName == NULL)) {
				tmp = HeapAlloc(GetProcessHeap(), 0, sizeof(FO_LIST_ITEM) + objinf->Name.Length + objinf->TypeName.Length + (sdlen + 4) * sizeof(WCHAR));
				if (tmp == NULL) {
					HeapFree(GetProcessHeap(), 0, objinf);
					break;
				}
				tmp->Prev = *List;
				tmp->ObjectName = tmp->NameBuffer;
				tmp->ObjectType = tmp->NameBuffer + sdlen + 2 + objinf->Name.Length / sizeof(WCHAR);
				_strcpyW(tmp->ObjectName, DirName);
				if ((DirName[0] == '\\') && (DirName[1] == 0)) {
					_strncpyW(tmp->ObjectName + sdlen, 1 + objinf->Name.Length / sizeof(WCHAR), objinf->Name.Buffer, objinf->Name.Length / sizeof(WCHAR));
				}
				else {
					tmp->ObjectName[sdlen] = '\\';
					_strncpyW(tmp->ObjectName + sdlen + 1, 1 + objinf->Name.Length / sizeof(WCHAR), objinf->Name.Buffer, objinf->Name.Length / sizeof(WCHAR));
				}
				_strncpyW(tmp->ObjectType, 1 + objinf->TypeName.Length / sizeof(WCHAR), objinf->TypeName.Buffer, objinf->TypeName.Length / sizeof(WCHAR));
				*List = tmp;
			};

		if (_strcmpiW(objinf->TypeName.Buffer, T_ObjectNames[TYPE_DIRECTORY]) == 0) {
			newdir = HeapAlloc(GetProcessHeap(), 0, (sdlen + 4)*sizeof(WCHAR) + objinf->Name.Length);
			if (newdir != NULL) {
				_strcpyW(newdir, DirName);
				if ((DirName[0] == '\\') && (DirName[1] == 0)) {
					_strncpyW(newdir + sdlen, 1 + objinf->Name.Length / sizeof(WCHAR), objinf->Name.Buffer, objinf->Name.Length / sizeof(WCHAR));
				}
				else {
					newdir[sdlen] = '\\';
					_strncpyW(newdir + sdlen + 1, 1 + objinf->Name.Length / sizeof(WCHAR), objinf->Name.Buffer, objinf->Name.Length / sizeof(WCHAR));
				}
				FindObject(newdir, NameSubstring, TypeName, List);
				HeapFree(GetProcessHeap(), 0, newdir);
			}
		};

		HeapFree(GetProcessHeap(), 0, objinf);
	} while (cond);

	if (hDirectory != NULL) {
		NtClose(hDirectory);
	}
}
