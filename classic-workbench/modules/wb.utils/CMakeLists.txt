set(PY_FILES	
    code_utils_grt.py
    table_templates.py
    table_utils_grt.py
    text_grt.py
    wb_dev_utils_grt.py
    wb_utils_grt.py
    wb_model_utils.py
    wb_catalog_utils.py
    sdp_tools_grt.py
    sdp_tools_backend.py
    sdp_workspace_grt.py
    sdp_workspace_backend.py
    sdp_local_worker.py
    sdp_compat_decision.py
    sdp_compat_jobs.py
    sdp_compat_cpu.py
    sdp_compat_logical.py
    sdp_compat_physical.py
)

install(FILES ${PY_FILES} DESTINATION ${WB_PYTHON_MODULES_DIR})
