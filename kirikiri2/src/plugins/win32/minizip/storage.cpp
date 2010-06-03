#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "ncbind/ncbind.hpp"
#include <map>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include "crypt.h"
#include "unzip.h"
#include "zip.h"

#include "narrow.h"

#define CASESENSITIVITY (0)

#define BASENAME L"zip"

/**
 * Zip �W�J�����N���X
 */
class UnzipBase {

public:
	UnzipBase() : refCount(1), uf(NULL) {
		::InitializeCriticalSection(&cs);
	}

	void AddRef() {
		refCount++;
	};

	void Release() {
		if (refCount == 1) {
			delete this;
		} else {
			refCount--;
		}
	};
	
	/**
	 * ZIP�t�@�C�����J��
	 * @param filename �t�@�C����
	 */
	bool init(const ttstr &filename) {
		done();
		if ((uf = unzOpen(NarrowString(filename))) != NULL) {
			lock();
			unzGoToFirstFile(uf);
			do {
				char filename_inzip[1024];
				unz_file_info file_info;
				if (unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip),NULL,0,NULL,0) == UNZ_OK) {
					entryName(ttstr(filename_inzip));
				}
			} while (unzGoToNextFile(uf) == UNZ_OK);
			unlock();
			return true;
		}
		return false;
	}

	/**
	 * �ʂ̓W�J�p�t�@�C�����J��
	 */
	unzData open(const ttstr &srcname) {
		if (uf) {
			lock();
			if (unzLocateFile(uf, NarrowString(srcname), CASESENSITIVITY) == UNZ_OK) {
				unzData data = NULL;
				if (unzOpenData(uf, &data, NULL, NULL, 0, NULL) == UNZ_OK) {
					return data;
				}
			}
			unlock();
		}
		return NULL;
	}

	/**
	 * �ʂ̓W�J�p�t�@�C������f�[�^��ǂݍ���
	 */
	HRESULT read(unzData data, void *pv, ULONG cb, ULONG *pcbRead) {
		if (uf && data) {
			lock();
			DWORD size = unzReadData(data,pv,cb);
			if (pcbRead) {
				*pcbRead = size;
			}
			unlock();
			return size < cb ? S_FALSE : S_OK;
		}
		return STG_E_ACCESSDENIED;
	}

	/**
	 * �ʂ̓W�J�p�t�@�C�������
	 */
	void close(unzData data) {
		if (uf && data) {
			lock();
			unzCloseData(data);
			unlock();
		}
	}
	
	bool CheckExistentStorage(const ttstr &name) {
		bool ret = true;
		if (uf) {
			lock();
			ret = unzLocateFile(uf, NarrowString(name), CASESENSITIVITY) == UNZ_OK;
			unlock();
		}
		return ret;
	}
	
	void GetListAt(const ttstr &name, iTVPStorageLister *lister) {
		ttstr fname = "/";
		fname += name;
		std::map<ttstr,FileNameList>::const_iterator it = dirEntryTable.find(fname);
		if (it != dirEntryTable.end()) {
			std::vector<ttstr>::const_iterator fit = it->second.begin();
			while (fit != it->second.end()) {
				lister->Add(*fit);
				fit++;
			}
		}
	}

protected:

	/**
	 * �f�X�g���N�^
	 */
	virtual ~UnzipBase() {
		done();
		::DeleteCriticalSection(&cs);
	}

	void done() {
		if (uf) {
			unzClose(uf);
			uf = NULL;
		}
	}

	// ���b�N
	void lock() {
		::EnterCriticalSection(&cs);
	}

	// ���b�N����
	void unlock() {
		::LeaveCriticalSection(&cs);
	}

	void entryName(const ttstr &name) {
		ttstr dname = TJS_W("/");
		ttstr fname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = wcsrchr(p, '/'))) {
			dname += ttstr(p, q-p);
			fname = ttstr(q+1);
		} else {
			fname = name;
		}
		dirEntryTable[dname].push_back(fname);
	}
	
private:
	int refCount;
	// zip�t�@�C�����
	unzFile uf;
	CRITICAL_SECTION cs;

	// �f�B���N�g���ʃt�@�C�����G���g�����
	typedef std::vector<ttstr> FileNameList;
	std::map<ttstr,FileNameList> dirEntryTable;
};

/**
 * ZIP�W�J�X�g���[���N���X
 */
class UnzipStream : public IStream {

public:
	/**
	 * �R���X�g���N�^
	 */
	UnzipStream(UnzipBase *unzip) : refCount(1), unzip(unzip), data(NULL) {
		unzip->AddRef();
	};

	// IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
		if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
			if (ppvObject == NULL)
				return E_POINTER;
			*ppvObject = this;
			AddRef();
			return S_OK;
		} else {
			*ppvObject = 0;
			return E_NOINTERFACE;
		}
	}

	ULONG STDMETHODCALLTYPE AddRef(void) {
		refCount++;
		return refCount;
	}
	
	ULONG STDMETHODCALLTYPE Release(void) {
		int ret = --refCount;
		if (ret <= 0) {
			delete this;
			ret = 0;
		}
		return ret;
	}

	// ISequentialStream
	HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead) {
		if (data) {
			return unzip->read(data, pv, cb, pcbRead);
		}
		return STG_E_ACCESSDENIED;
	}

	HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) {
		return E_NOTIMPL;
	}

	// IStream
	HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove,	DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) {
		if (dwOrigin == STREAM_SEEK_SET && dlibMove.QuadPart == 0) {
			rewind();
			if (plibNewPosition) {
				plibNewPosition->QuadPart = 0;
			}
		}
		return STG_E_INVALIDFUNCTION;
	}
	
	HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) {
		return E_NOTIMPL;
	}
	
	HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Revert(void) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) {
		return E_NOTIMPL;
	}
	
	HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) {
		return E_NOTIMPL;
	}
	
	HRESULT STDMETHODCALLTYPE Stat(STATSTG *pstatstg, DWORD grfStatFlag) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) {
		return E_NOTIMPL;
	}

	bool init(const ttstr filename) {
		bool ret = false;
		if ((ret = (data = unzip->open(filename)) != NULL)) {
			this->filename = filename;
		}
		return ret;
	}
	
protected:
	/**
	 * �f�X�g���N�^
	 */
	virtual ~UnzipStream() {
		close();
		unzip->Release();
	}

	void close() {
		if (data) {
			unzip->close(data);
			data = NULL;
		}
	}
	
	void rewind() {
		close();
		data = unzip->open(filename);
	}
	
private:
	int refCount;
	ttstr filename;
	UnzipBase *unzip;
	unzData data;
};

/**
 * ZIP�X�g���[�W
 */
class ZipStorage : public iTVPStorageMedia
{

public:
	/**
	 * �R���X�g���N�^
	 */
	ZipStorage() : refCount(1) {
	}

	/**
	 * �f�X�g���N�^
	 */
	virtual ~ZipStorage() {
		// �S����j��
		std::map<ttstr, UnzipBase*>::iterator it = unzipTable.begin();
		while (it != unzipTable.end()) {
			it->second->Release();
			it = unzipTable.erase(it);
		}
	}

public:
	// -----------------------------------
	// iTVPStorageMedia Intefaces
	// -----------------------------------

	virtual void TJS_INTF_METHOD AddRef() {
		refCount++;
	};

	virtual void TJS_INTF_METHOD Release() {
		if (refCount == 1) {
			delete this;
		} else {
			refCount--;
		}
	};

	// returns media name like "file", "http" etc.
	virtual void TJS_INTF_METHOD GetName(ttstr &name) {
		name = BASENAME;
	}

	//	virtual ttstr TJS_INTF_METHOD IsCaseSensitive() = 0;
	// returns whether this media is case sensitive or not

	// normalize domain name according with the media's rule
	virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) {
		// nothing to do
	}

	// normalize path name according with the media's rule
	virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) {
		// nothing to do
	}

	// check file existence
	virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		return unzip ? unzip->CheckExistentStorage(fname) : false;
	}

	// open a storage and return a tTJSBinaryStream instance.
	// name does not contain in-archive storage name but
	// is normalized.
	virtual tTJSBinaryStream * TJS_INTF_METHOD Open(const ttstr & name, tjs_uint32 flags) {
		// �������݂͋֎~
		if (flags == TJS_BS_WRITE || flags == TJS_BS_APPEND) {
			TVPThrowExceptionMessage(TJS_W("cannot write to memfile:%1"), name);
		}
		tTJSBinaryStream *ret = NULL;
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		if (unzip) {
			UnzipStream *stream = new UnzipStream(unzip);
			if (stream) {
				if (stream->init(fname)) {
					ret = TVPCreateBinaryStreamAdapter(stream);
				}
				stream->Release();
			}
		}
		if (!ret) {
			TVPThrowExceptionMessage(TJS_W("cannot open memfile:%1"), name);
		}
		return ret;
	}

	// list files at given place
	virtual void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister * lister) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		if (unzip) {
			unzip->GetListAt(fname, lister);
		}
	}

	// basically the same as above,
	// check wether given name is easily accessible from local OS filesystem.
	// if true, returns local OS native name. otherwise returns an empty string.
	virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) {
		name = "";
	}

public:

	/**
	 * zip�t�@�C�����t�@�C���V�X�e���Ƃ��� mount ���܂�
	 * zip://�h���C����/�t�@�C���� �ŃA�N�Z�X�\�ɂȂ�܂��B�ǂݍ��ݐ�p�ɂȂ�܂��B
	 * @param name �h���C����
	 * @param zipfile �}�E���g����ZIP�t�@�C����
	 * @return �}�E���g�ɐ��������� true
	 */
	bool mount(const ttstr &name, const ttstr &zipfile) {
		unmount(name);
		UnzipBase *newUnzip = new UnzipBase();
		if (newUnzip) {
			if (newUnzip->init(zipfile)) {
				unzipTable[name] = newUnzip;
				return true;
			} else {
				newUnzip->Release();
			}
		}
		return false;
	}

	/**
	 * zip�t�@�C���� unmount ���܂�
	 * @param name �h���C����
	 * @return �A���}�E���g�ɐ��������� true
	 */
	bool unmount(const ttstr &name) {
		std::map<ttstr, UnzipBase*>::iterator it = unzipTable.find(name);
		if (it != unzipTable.end()) {
			it->second->Release();
			unzipTable.erase(it);
			return true;
		}
		return false;
	}

protected:

	/*
	 * �h���C���ɍ��v���� Unzip �����擾
	 * @param name �t�@�C����
	 * @param fname �t�@�C������Ԃ�
	 * @return Unzip���
	 */
	UnzipBase *getUnzip(const ttstr &name, ttstr &fname) {
		ttstr dname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = wcschr(p, '/'))) {
			dname = ttstr(p, q-p);
			fname = ttstr(q+1);
		} else {
			TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);
		}
		std::map<ttstr, UnzipBase*>::const_iterator it = unzipTable.find(dname);
		if (it != unzipTable.end()) {
			return it->second;
		}
		return NULL;
	}
	
private:
	tjs_uint refCount; //< ���t�@�����X�J�E���g
	std::map<ttstr, UnzipBase*> unzipTable; //< zip���
};


/**
 * ���\�b�h�ǉ��p
 */
class StoragesZip {

public:
	
	static void init() {
		if (zip == NULL) {
			zip = new ZipStorage();
			TVPRegisterStorageMedia(zip);
		}
	}

	static void done() {
		if (zip != NULL) {
			TVPUnregisterStorageMedia(zip);
			zip->Release();
			zip = NULL;
		}
	}

	/**
	 * zip�t�@�C�����t�@�C���V�X�e���Ƃ��� mount ���܂�
	 * zip://�h���C����/�t�@�C���� �ŃA�N�Z�X�\�ɂȂ�܂��B�ǂݍ��ݐ�p�ɂȂ�܂��B
	 * @param name �h���C����
	 * @param zipfile �}�E���g����ZIP�t�@�C����
	 * @return �}�E���g�ɐ��������� true
	 */
	static bool mountZip(const tjs_char *name, const tjs_char *zipfile) {
		if (zip) {
			return zip->mount(ttstr(name), ttstr(zipfile));
		}
		return false;
	}

	/**
	 * zip�t�@�C���� unmount ���܂�
	 * @param name �h���C����
	 * @return �A���}�E���g�ɐ��������� true
	 */
	static bool unmountZip(const tjs_char *name) {
		if (zip) {
			return zip->unmount(ttstr(name));
		}
		return false;
	}

protected:
	static ZipStorage *zip;
};

ZipStorage *StoragesZip::zip = NULL;

NCB_ATTACH_CLASS(StoragesZip, Storages) {
	NCB_METHOD(mountZip);
	NCB_METHOD(unmountZip);
};

void initZipStorage()
{
	StoragesZip::init();
}

void doneZipStorage()
{
	StoragesZip::done();
}