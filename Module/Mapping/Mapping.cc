#include "Mapping.h"
#include "Constant.h"
#include "Reduction.h"
#include "RenderScene.h"

Mapping::Mapping() :
		meshUpdated(false), hasNewKFFlag(false) {
	Create();
}

void Mapping::Create() {

	heapCounter.create(1);
	hashCounter.create(1);
	noVisibleEntries.create(1);
	heap.create(DeviceMap::NumSdfBlocks);
	sdfBlock.create(DeviceMap::NumVoxels);
	bucketMutex.create(DeviceMap::NumBuckets);
	hashEntries.create(DeviceMap::NumEntries);
	visibleEntries.create(DeviceMap::NumEntries);

	nBlocks.create(1);
	noTriangles.create(1);
	modelVertex.create(DeviceMap::MaxVertices);
	modelNormal.create(DeviceMap::MaxVertices);
	modelColor.create(DeviceMap::MaxVertices);
	blockPoses.create(DeviceMap::NumEntries);

	edgeTable.create(256);
	vertexTable.create(256);
	triangleTable.create(16, 256);
	edgeTable.upload(edgeTableHost);
	vertexTable.upload(vertexTableHost);
	triangleTable.upload(triangleTableHost);

	zRangeMin.create(80, 60);
	zRangeMax.create(80, 60);
	zRangeMinEnlarged.create(160, 120);
	zRangeMaxEnlarged.create(160, 120);
	noRenderingBlocks.create(1);
	renderingBlockList.create(DeviceMap::MaxRenderingBlocks);

	noKeys.create(1);
	mutexKeys.create(KeyMap::MaxKeys);
	mapKeys.create(KeyMap::maxEntries);
	tmpKeys.create(KeyMap::maxEntries);
	surfKeys.create(2000);

	Reset();
}

void Mapping::ForwardWarp(const Frame * last, Frame * next) {
	ForwardWarping(last->vmap[0], last->nmap[0], next->vmap[0], next->nmap[0],
			last->GpuRotation(), next->GpuInvRotation(), last->GpuTranslation(),
			next->GpuTranslation(), Frame::fx(0), Frame::fy(0), Frame::cx(0),
			Frame::cy(0));
}

void Mapping::UpdateVisibility(const Frame * f, uint & no) {

	CheckBlockVisibility(*this, noVisibleEntries, f->GpuRotation(), f->GpuInvRotation(),
			f->GpuTranslation(), Frame::cols(0), Frame::rows(0), Frame::fx(0),
			Frame::fy(0), Frame::cx(0), Frame::cy(0), DeviceMap::DepthMax,
			DeviceMap::DepthMin, &no);
}

void Mapping::UpdateVisibility(Matrix3f Rview, Matrix3f RviewInv, float3 tview,
		float depthMin, float depthMax, float fx, float fy, float cx, float cy,
		uint & no) {

	CheckBlockVisibility(*this, noVisibleEntries, Rview, RviewInv, tview, 640,
			480, fx, fy, cx, cy, depthMax, depthMin, &no);
}

void Mapping::FuseColor(const Frame * f, uint & no) {
	FuseColor(f->range, f->color, f->GpuRotation(), f->GpuInvRotation(), f->GpuTranslation(), no);
}

void Mapping::FuseColor(const DeviceArray2D<float> & depth,
		const DeviceArray2D<uchar3> & color, Matrix3f Rview, Matrix3f RviewInv,
		float3 tview, uint & no) {

	FuseMapColor(depth, color, noVisibleEntries, Rview, RviewInv, tview, *this,
			Frame::fx(0), Frame::fy(0), Frame::cx(0), Frame::cy(0),
			DeviceMap::DepthMax, DeviceMap::DepthMin, &no);

}

void Mapping::RayTrace(uint noVisibleBlocks, Frame * f) {
	RayTrace(noVisibleBlocks, f->GpuRotation(), f->GpuInvRotation(), f->GpuTranslation(),
			f->vmap[0], f->nmap[0], DeviceMap::DepthMin, DeviceMap::DepthMax,
			Frame::fx(0), Frame::fy(0), Frame::cx(0), Frame::cy(0));
}

void Mapping::RayTrace(uint noVisibleBlocks, Matrix3f Rview, Matrix3f RviewInv,
		float3 tview, DeviceArray2D<float4> & vmap,	DeviceArray2D<float4> & nmap,
		float depthMin, float depthMax, float fx, float fy, float cx, float cy) {

	if (CreateRenderingBlocks(visibleEntries, zRangeMin, zRangeMax, depthMax, depthMin,
			renderingBlockList, noRenderingBlocks, RviewInv, tview,
			noVisibleBlocks, fx, fy, cx, cy)) {

		Raycast(*this, vmap, nmap, zRangeMin, zRangeMax, Rview, RviewInv, tview,
				1.0 / fx, 1.0 / fy, cx, cy);
	}
}

void Mapping::CreateModel() {

	MeshScene(nBlocks, noTriangles, *this, edgeTable, vertexTable,
			triangleTable, modelNormal, modelVertex, modelColor, blockPoses);

	noTriangles.download(&noTrianglesHost);
	if (noTrianglesHost > 0) {
		meshUpdated = true;
	}
}

void Mapping::UpdateMapKeys() {
	noKeys.clear();
	CollectKeyPoints(*this, tmpKeys, noKeys);

	noKeys.download(&noKeysHost);
	if(noKeysHost != 0) {
		hostKeys.resize(noKeysHost);
		tmpKeys.download(hostKeys.data(), noKeysHost);
	}
}

void Mapping::CreateRAM() {

	heapCounterRAM = new int[1];
	hashCounterRAM = new int[1];
	noVisibleEntriesRAM = new uint[1];
	heapRAM = new int[DeviceMap::NumSdfBlocks];
	bucketMutexRAM = new int[DeviceMap::NumBuckets];
	sdfBlockRAM = new Voxel[DeviceMap::NumVoxels];
	hashEntriesRAM = new HashEntry[DeviceMap::NumEntries];
	visibleEntriesRAM = new HashEntry[DeviceMap::NumEntries];
}

void Mapping::DownloadToRAM() {

	CreateRAM();

	heapCounter.download(heapCounterRAM);
	hashCounter.download(hashCounterRAM);
	noVisibleEntries.download(noVisibleEntriesRAM);
	heap.download(heapRAM);
	bucketMutex.download(bucketMutexRAM);
	sdfBlock.download(sdfBlockRAM);
	hashEntries.download(hashEntriesRAM);
	visibleEntries.download(visibleEntriesRAM);
}

void Mapping::UploadFromRAM() {

	heapCounter.upload(heapCounterRAM);
	hashCounter.upload(hashCounterRAM);
	noVisibleEntries.upload(noVisibleEntriesRAM);
	heap.upload(heapRAM);
	bucketMutex.upload(bucketMutexRAM);
	sdfBlock.upload(sdfBlockRAM);
	hashEntries.upload(hashEntriesRAM);
	visibleEntries.upload(visibleEntriesRAM);
}

void Mapping::ReleaseRAM() {

	delete [] heapCounterRAM;
	delete [] hashCounterRAM;
	delete [] noVisibleEntriesRAM;
	delete [] heapRAM;
	delete [] bucketMutexRAM;
	delete [] sdfBlockRAM;
	delete [] hashEntriesRAM;
	delete [] visibleEntriesRAM;
}

bool Mapping::HasNewKF() {

	return hasNewKFFlag;
}

void Mapping::FuseKeyFrame(const KeyFrame * kf) {

	if(keyFrames.count(kf))
		return;

	keyFrames.insert(kf);
	hasNewKFFlag = true;

	cv::Mat desc;
	std::vector<SurfKey> keyChain;
	kf->descriptors.download(desc);
	int noK = std::min(kf->N, (int)surfKeys.size);

	for(int i = 0; i < noK; ++i) {
		if(kf->observations[i] > -1) {
			SurfKey key;
			Eigen::Vector3f pt = kf->GetWorldPoint(i);
			key.pos = {pt(0), pt(1), pt(2)};
			key.normal = kf->pointNormal[i];
			key.valid = true;
			for (int j = 0; j < 64; ++j) {
				key.descriptor[j] = desc.at<float>(i, j);
			}
			keyChain.push_back(key);
		}
	}

	surfKeys.upload(keyChain.data(), keyChain.size());
	InsertKeyPoints(*this, surfKeys, keyChain.size());
}

void Mapping::FuseKeyPoints(const Frame * f) {

	cv::Mat desc;
	std::vector<SurfKey> keyChain;
	f->descriptors.download(desc);
	int noK = std::min(f->N, (int)surfKeys.size);

	for(int i = 0; i < noK; ++i) {
		if(!f->outliers[i]) {
			SurfKey key;
			Eigen::Vector3f pt = f->GetWorldPoint(i);
			key.pos = { pt(0), pt(1), pt(2) };
			key.normal = f->pointNormal[i];
			key.valid = true;
			for(int j = 0; j < 64; ++j) {
				key.descriptor[j] = desc.at<float>(i, j);
			}
			keyChain.push_back(key);
		}
	}

	surfKeys.upload(keyChain.data(), keyChain.size());
	InsertKeyPoints(*this, surfKeys, keyChain.size());
}

void Mapping::Reset() {

	ResetMap(*this);
	ResetKeyPoints(*this);

	mapKeys.clear();
	keyFrames.clear();
}

Mapping::operator KeyMap() const {

	KeyMap map;

	map.Keys = mapKeys;
	map.Mutex = mutexKeys;

	return map;
}

Mapping::operator DeviceMap() const {

	DeviceMap map;

	map.heapMem = heap;
	map.heapCounter = heapCounter;
	map.noVisibleBlocks = noVisibleEntries;
	map.bucketMutex = bucketMutex;
	map.hashEntries = hashEntries;
	map.visibleEntries = visibleEntries;
	map.voxelBlocks = sdfBlock;
	map.entryPtr = hashCounter;

	return map;
}
