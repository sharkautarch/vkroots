namespace vkroots {
#ifdef __GNUC__
  #define VKROOTS_INLINE_ATTR __attribute__((always_inline))
  #define VKROOTS_FLATTEN_ATTR __attribute__((visibility("protected"), flatten))
#else
  #define VKROOTS_INLINE_ATTR
  #define VKROOTS_FLATTEN_ATTR
#endif

  template <class R, class... TArgs>
  class constexpr_function<R(TArgs...)> {

  //lamda holding base+derived classes use the "Simulated C++ Interface Template Pattern"
  //approach described here: https://www.codeproject.com/articles/603818/cplusplus-runtime-polymorphism-without-virtual-fun
  //to avoid using virtual functions to reduce the amount of indirection at runtime
  struct interface {
	typedef R (*PFN_RUN_T)(const void* const, TArgs...);
 
    PFN_RUN_T m_pfnRun_T;
 
    template<typename T> 
    static constexpr R VKROOTS_INLINE_ATTR Run_T( const void* const pObj, TArgs... args ) {
      return static_cast<const T* const>(pObj)->siRun( args... ); }

    constexpr interface() : m_pfnRun_T( 0 ) {}
     
 
    constexpr R VKROOTS_FLATTEN_ATTR siRun(  TArgs... args ) const {
      return (*m_pfnRun_T)( this, args... );
    }

    protected:
      template<typename T>
      constexpr void Init() {
        m_pfnRun_T = (PFN_RUN_T) &Run_T<T>;
      }
  };

  template <typename Fn>
  struct implementation final : interface {
    friend struct interface;
    
    constexpr implementation( Fn fn) : fn{std::in_place_t{}, fn } {
      interface::template Init<implementation>();
    }
    
    /*consteval const interface& getParent() {
    	return static_cast<interface>(*this);
    }*/
    
    consteval implementation() {}
   
    constexpr implementation(const implementation& __restrict__ other) : fn{std::in_place_t{}, *(other.fn)} {
    	interface::template Init<implementation>();
    }
    
    constexpr const implementation<Fn>& operator=(const implementation<Fn>& __retrict__ ) const {
        return std::move(*this);
    }
    
    private:
      constexpr R VKROOTS_FLATTEN_ATTR siRun( TArgs... args ) const { 
        return ((*fn))(args...);
      }

    protected:
      std::optional<Fn> fn;
  };

 public:
  constexpr constexpr_function(const constexpr_function& __restrict__ other) : m_fn{other.m_fn} {}

  template <typename Fn>
  static constexpr const interface& implToInterface(implementation<Fn> impl) {
    impl_holder<Fn> = impl;
    return *(impl_holder<Fn>);
  }

  template <class Fn> requires std::invocable<Fn, TArgs...> || ConceptNullFunc<Fn, R>
  constexpr  constexpr_function(Fn fn) : m_fn{ implToInterface(implementation<Fn>(fn)) }  {}

  constexpr auto VKROOTS_FLATTEN_ATTR operator()(TArgs... args) const -> R {
	 if constexpr (sizeof...(TArgs) == 0)
	 	return (m_fn.siRun)();
	 else if constexpr (sizeof...(TArgs) != 0) {
	    return (m_fn.siRun)(args...);
	 } else {
	    __builtin_unreachable();
	 }
  }
  constexpr operator bool() {
  	return true;
  }

 private:
  template <typename T>
  static constinit inline std::optional<implementation<T>> impl_holder;
 
  const interface& m_fn{};
};// end of constexpr_func class
} // vkroots

#undef VKROOTS_INLINE_ATTR
#undef VKROOTS_FLATTEN_ATTR


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
    using MapData = SynchronizedMapObject<Key, Data>;

    SynchronizedMapObject(std::shared_ptr<Data> data)
      : m_data{std::move(data)} {
    }
    SynchronizedMapObject(const SynchronizedMapObject<Key, Data>& other)
      : m_data{other.m_data} {
    }
    SynchronizedMapObject(SynchronizedMapObject<Key, Data>&& other)
      : m_data{std::move(other.m_data)} {
    }
    SynchronizedMapObject(std::nullptr_t) {
    }

    SynchronizedMapObject& operator = (std::nullptr_t) {
      m_data = nullptr;
      return *this;
    }

    SynchronizedMapObject& operator = (const SynchronizedMapObject& other) {
      m_data = other.m_data;
    }

    SynchronizedMapObject& operator = (SynchronizedMapObject&& other) {
      m_data = std::move(other.m_data);
    }

    static SynchronizedMapObject<Key, Data> get(const Key& key) {
      std::unique_lock lock{ s_mutex };
      auto iter = s_map.find(key);
      if (iter == s_map.end())
        return nullptr;
      return iter->second;
    }

    static SynchronizedMapObject<Key, Data> create(const Key& key, Data data) {
      std::unique_lock lock{ s_mutex };
      auto val = s_map.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(std::make_shared<Data>(std::move(data))));
      return val.first->second;
    }

    static bool remove(const Key& key) {
      std::unique_lock lock{ s_mutex };
      auto iter = s_map.find(key);
      if (iter == s_map.end())
        return false;
      s_map.erase(iter);
      return true;
    }

    Data* get() {
      return m_data.get();
    }

    const Data* get() const {
      return m_data.get();
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

  private:

    std::shared_ptr<Data> m_data;

    static std::mutex s_mutex;
    static std::unordered_map<MapKey, MapData> s_map;
  };

#define VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(name, key) \
  using name = ::vkroots::helpers::SynchronizedMapObject<key, name##Data>;

#define VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(x) \
  template <> std::mutex x::s_mutex = {}; \
  template <> std::unordered_map<x::MapKey, x::MapData> x::s_map = {};

}

namespace vkroots {
#if VKROOTS_USE_CONSTEXPR_FUNC
  #define VKROOTS_CHAINPATCHER_FUNC constexpr_function
  #define VKROOTS_CHAINPATCHER_ATTR constexpr
#else
  #define VKROOTS_CHAINPATCHER_FUNC std::function
  #define VKROOTS_CHAINPATCHER_ATTR
#endif

  template <typename Type, typename UserData = uint64_t>
  class ChainPatcher {
  public:
    template <typename AnyStruct>
    VKROOTS_CHAINPATCHER_ATTR ChainPatcher(const AnyStruct * __restrict__ obj, VKROOTS_CHAINPATCHER_FUNC<bool(UserData&, Type *)> func) {
      const Type * __restrict__ type = vkroots::FindInChain<Type>(obj);
      if (type) {
        func(m_ctx, const_cast<Type *>(type));
      } else {
        if (func(m_ctx, &m_value)) {
          AnyStruct *mutObj = const_cast<AnyStruct*>(obj);
          m_value.sType = ResolveSType<Type>();
          m_value.pNext = const_cast<void*>(std::exchange(mutObj->pNext, reinterpret_cast<const void*>(&m_value)));
        }
      }
    }

    template <typename AnyStruct>
    VKROOTS_CHAINPATCHER_ATTR ChainPatcher(const AnyStruct * __restrict__ obj, VKROOTS_CHAINPATCHER_FUNC<bool(Type *)> func)
      : ChainPatcher(obj, [func](UserData& ctx, Type *obj) { return func(obj); }) {
    }

  private:
    Type m_value{};
    UserData m_ctx;
  };
  
  #undef VKROOTS_CHAINPATCHER_FUNC
  #undef VKROOTS_CHAINPATCHER_ATTR
}