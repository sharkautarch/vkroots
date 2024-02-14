
namespace vkroots::helpers {

  template <typename Func>
  inline void delimitStringView(std::string_view view, std::string_view delim, Func func) {
    size_t pos = 0;
    while ((pos = view.find(delim)) != std::string_view::npos) {
      std::string_view token = view.substr(0, pos);
      if (!func(token))
        return;
      view = view.substr(pos + 1);
    }
    func(view);
  }

  template <typename T, typename ArrType, typename Op>
  inline VkResult array(ArrType& arr, uint32_t *pCount, T* pOut, Op func) {
    const uint32_t count = uint32_t(arr.size());

    if (!pOut) {
      *pCount = count;
      return VK_SUCCESS;
    }

    const uint32_t outCount = std::min(*pCount, count);
    for (uint32_t i = 0; i < outCount; i++)
      func(pOut[i], arr[i]);

    *pCount = outCount;
    return count != outCount
      ? VK_INCOMPLETE
      : VK_SUCCESS;
  }

  template <typename T, typename ArrType>
  inline VkResult array(ArrType& arr, uint32_t *pCount, T* pOut) {
    return array(arr, pCount, pOut, [](T& x, const T& y) { x = y; });
  }

  template <typename Func, typename OutArray, typename... Args>
  uint32_t enumerate(Func function, OutArray& outArray, Args&&... arguments) {
    uint32_t count = 0;
    function(arguments..., &count, nullptr);

    outArray.resize(count);
    if (!count)
        return 0;

    function(std::forward<Args>(arguments)..., &count, outArray.data());
    return count;
  }

  template <typename Func, typename InArray, typename OutType, typename... Args>
  VkResult append(Func function, const InArray& inArray, uint32_t* pOutCount, OutType* pOut, Args&&... arguments) {
    uint32_t baseCount = 0;
    function(std::forward<Args>(arguments)..., &baseCount, nullptr);

    const uint32_t totalCount = baseCount + uint32_t(inArray.size());
    if (!pOut) {
      *pOutCount = totalCount;
      return VK_SUCCESS;
    }

    if (*pOutCount < totalCount) {
      function(std::forward<Args>(arguments)..., pOutCount, pOut);
      return VK_INCOMPLETE;
    }

    function(std::forward<Args>(arguments)..., &baseCount, pOut);
    for (size_t i = 0; i < inArray.size(); i++)
      pOut[baseCount + i] = inArray[i];
    return VK_SUCCESS;
  }

  template <typename SearchType, VkStructureType StructureTypeEnum, typename ChainBaseType>
  SearchType *chain(ChainBaseType* pNext) {
    for (VkBaseOutStructure* pBaseOut = reinterpret_cast<VkBaseOutStructure*>(pNext); pBaseOut; pBaseOut = pBaseOut->pNext) {
      if (pBaseOut->sType == StructureTypeEnum)
        return reinterpret_cast<SearchType*>(pBaseOut);
    }
    return nullptr;
  }

  template <typename Key, typename Data>
  class SynchronizedMapObject {
  public:
    using MapKey = Key;
    using MapData = Data;

     static SynchronizedMapObject get(const Key& key) {
      {
#ifdef VKROOTS_DEBUG
      	  printf("in get()\n");
#endif
	  auto iter = s_map.find(key);
	  auto waitIter = s_waitMap.find(key);
	  if (waitIter == s_waitMap.end() && iter == s_map.end()) {
	     
	     uint32_t pendingIdx = (s_pendingWaitsIdx++) + 1; //note: atomic::operator++(int) returns the value *before* the increment
	     uint32_t targetIndex = pendingIdx + s_waitMapIdx;
	     
	     s_pendingWaits[pendingIdx] = key;

	     std::atomic<bool> * bAtm = reinterpret_cast<std::atomic<bool> *>(&s_boolPool[targetIndex]);
	     bAtm->wait(false);
	     
	  }
	  else if (iter == s_map.end()) {
	    std::atomic<bool> * bAtm = reinterpret_cast<std::atomic<bool> *>(waitIter->second);
	    bAtm->wait(false);
	  }

      }
      {
          std::unique_lock lock{ s_mutex };
          auto iter = s_map.find(key);
#ifdef VKROOTS_DEBUG
          printf("out of get()\n");
#endif
          if (iter == s_map.end()) {
            return SynchronizedMapObject{ nullptr };
          }
          return SynchronizedMapObject{ iter->second, std::move(lock) };
      }
      
    }

    static SynchronizedMapObject create(const Key& key, Data data) {
#ifdef VKROOTS_DEBUG
      printf("in create()\n");
#endif
      {
      	auto iter = s_waitMap.find(key);
      	if (iter != s_waitMap.end()) {
      	   std::atomic<bool> * bAtm = reinterpret_cast<std::atomic<bool> *>(iter->second);
      	   bAtm->store(false);
      	}
      } 

      std::unique_lock lock{ s_mutex };
      
      uint32_t pendingIdx = s_pendingWaitsIdx;
      Key pending[pendingIdx];
      
      if ( pendingIdx > 0 ) {
        for (uint32_t i = 0; i < pendingIdx; i++) {
          pending[i]=s_pendingWaits[i];
        }
        
        uint32_t mapIdx = s_waitMapIdx;
        
        for (uint32_t i = mapIdx; i < mapIdx + pendingIdx; i++) {
            s_waitMap[pending[i-mapIdx]] = &s_boolPool[i];
        }
        
        s_pendingWaitsIdx -= pendingIdx;
        s_waitMapIdx += pendingIdx;
      }
      
      auto val = s_map.insert(std::make_pair(key, std::move(data)));
      auto ret = SynchronizedMapObject{ val.first->second, std::move(lock) };
      {
      	auto iter = s_waitMap.find(key);
      	if (iter != s_waitMap.end()) {
      	   std::atomic<bool> * bAtm = reinterpret_cast<std::atomic<bool> *>(iter->second);
      	   bAtm->store(true);
      	}
      }
#ifdef VKROOTS_DEBUG 
      printf("out of create()\n");
#endif
      return ret;
    }

    static bool remove(const Key& key) {
#ifdef VKROOTS_DEBUG
      printf("in remove()\n");
#endif
      std::unique_lock lock{ s_mutex };
      auto iter = s_map.find(key);
#ifdef VKROOTS_DEBUG
      printf("out of remove()\n");
#endif
      if (iter == s_map.end())
        return false;
      s_map.erase(iter);
      return true;
    }

    Data* get() {
      return m_data;
    }

    const Data* get() const {
      return m_data;
    }

    Data* operator->() {
      return get();
    }

    const Data* operator->() const {
      return get();
    }

    bool has() const {
      return m_data != nullptr;
    }

    operator bool() const {
      return has();
    }

    void clear() {
      m_data = nullptr;
      m_lock = {};
    }

    SynchronizedMapObject(SynchronizedMapObject&& other)
      : m_data{ other.m_data }, m_lock{ std::move(other.m_lock) } {
    }

  private:
    SynchronizedMapObject(std::nullptr_t)
        : m_data{ nullptr }, m_lock{} {}

    SynchronizedMapObject(Data& data, std::unique_lock<std::mutex> lock) noexcept
        : m_data{ &data }, m_lock{ std::move(lock) } {}

    Data *m_data;
    std::unique_lock<std::mutex> m_lock;

    static std::array<bool, 64> s_boolPool;

    static std::unordered_map<Key, bool*> s_waitMap; // latch to avoid deadlocks caused by lock-order-inversion, 
    //wherein one thread (T1) tries to access data from object A first, and then another (T2) thread is still creating object A
    //Yet somehow thread T2 ends up getting stuck waiting to acquire the lock for object A
    static std::atomic<uint32_t> s_waitMapIdx; 
    
    static Key s_pendingWaits[64];
    static std::atomic<uint32_t> s_pendingWaitsIdx;
    
    static std::mutex s_mutex;
    static std::unordered_map<Key, Data> s_map;
  };

#define VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(name, key) \
  using name = ::vkroots::helpers::SynchronizedMapObject<key, name##Data>;

#define VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(x) \
  template <> std::mutex x::s_mutex = {}; \
  template <> std::unordered_map<x::MapKey, x::MapData> x::s_map = {}; \
  template <> std::unordered_map<x::MapKey, bool*> x::s_waitMap = {}; \
  template <> x::MapKey x::s_pendingWaits[64] = {}; \
  template <> std::atomic<uint32_t> x::s_waitMapIdx = 0; \
  template <> std::atomic<uint32_t> x::s_pendingWaitsIdx = 0; \
  template <> std::array<bool, 64> x::s_boolPool = {};

}
