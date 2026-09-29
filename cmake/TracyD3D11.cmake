# Small, version-checked overlay; never modify FetchContent/offline source trees.
# D3D11 GetData may return S_FALSE after the enclosing disjoint query is ready.
# Upstream 0.13.1 drops the remaining timestamps in that case. Retry on a later
# frame, retaining the next unread index; blocking collection is shutdown only.
file(READ "${tracy_SOURCE_DIR}/public/tracy/TracyD3D11.hpp" forge_tracy_d3d11)
set(forge_tracy_query "            if (m_immediateDevCtx->GetData(m_queries[k], &timestamp, sizeof(timestamp), 0) != S_OK)")
string(FIND "${forge_tracy_d3d11}" "${forge_tracy_query}" forge_tracy_query_at)
if(forge_tracy_query_at EQUAL -1)
  message(FATAL_ERROR "Tracy D3D11 query code changed; review the nonblocking retry overlay.")
endif()
set(forge_tracy_retry [=[            if (mode == CollectMode::BLOCK) WaitForQuery(m_queries[k]);
            const HRESULT timestampResult = m_immediateDevCtx->GetData(m_queries[k], &timestamp, sizeof(timestamp), 0);
            if (timestampResult == S_FALSE)
            {
                // Retain unread queries without waiting or re-emitting earlier timestamps.
                m_previousCheckpoint = i;
                TracyPlot("GPU timestamp retry", int64_t(1));
                return;
            }
            if (timestampResult != S_OK)]=])
string(REPLACE "${forge_tracy_query}" "${forge_tracy_retry}" forge_tracy_d3d11 "${forge_tracy_d3d11}")
string(REPLACE "\"Tracy.hpp\"" "<tracy/Tracy.hpp>" forge_tracy_d3d11 "${forge_tracy_d3d11}")
string(REPLACE "../client/" "client/" forge_tracy_d3d11 "${forge_tracy_d3d11}")
string(REPLACE "../common/" "common/" forge_tracy_d3d11 "${forge_tracy_d3d11}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/profile-include")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/profile-include/ForgeTracyD3D11.hpp"
  CONTENT "${forge_tracy_d3d11}" @ONLY)
target_include_directories(fableforge_profile INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/profile-include")
