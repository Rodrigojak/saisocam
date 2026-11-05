static void ProcessIsometricCamera() {
        HandleInput();
        if (!g_Config.bIsometricEnabled) return;

        CPlaceable* pPlayer = GameSA::FindPlayerPed(0);
        if (!pPlayer) return;

        CPlaceable* pVehicle = GameSA::FindPlayerVehicle(0, false);
        float dtSeconds = ClampVal(GameSA::GetTimeStep() / 50.0f, 0.001f, 0.1f);
        bool bInVehicle = (pVehicle != nullptr);

        CPlaceable* pFocusEntity = bInVehicle ? pVehicle : pPlayer;
        if (!pFocusEntity) return;

        CVector targetPos = pFocusEntity->GetPosition();
        targetPos.z += bInVehicle ? 0.85f : 0.65f;

        float targetYawRad = 45.0f * DEG2RAD + (static_cast<int>(g_Config.eCurrentQuadrant) * 90.0f * DEG2RAD);
        float targetPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;

        CPlaceable* pCamPlaceable = GameSA::GetCameraPlaceable();
        if (!pCamPlaceable) return;

        if (!g_bCamInitialized) {
            g_CurrentCamPos = pCamPlaceable->GetPosition();
            g_CurrentLookAt = targetPos;
            g_CurrentYawRad = targetYawRad;
            g_CurrentPitchRad = targetPitchRad;
            g_CurrentDist = g_Config.fViewDistance;
            g_bCamInitialized = true;
        }

        g_CurrentYawRad = LerpAngleRad(g_CurrentYawRad, targetYawRad, 0.1f);
        g_CurrentPitchRad = LerpAngleRad(g_CurrentPitchRad, targetPitchRad, 0.1f);

        float horizontalRadius = g_CurrentDist * std::cos(g_CurrentPitchRad);
        float verticalHeight = g_CurrentDist * std::sin(g_CurrentPitchRad);

        CVector idealCamOffset(
            std::sin(g_CurrentYawRad) * horizontalRadius,
            -std::cos(g_CurrentYawRad) * horizontalRadius,
            verticalHeight
        );

        CVector idealCamPos = targetPos + idealCamOffset;
        g_CurrentCamPos = SmoothLerpVector(g_CurrentCamPos, idealCamPos, g_Config.fPositionSmoothSpeed, dtSeconds);
        g_CurrentLookAt = SmoothLerpVector(g_CurrentLookAt, targetPos, g_Config.fPositionSmoothSpeed * 1.4f, dtSeconds);

        CVector vecForward = g_CurrentLookAt - g_CurrentCamPos;
        if (vecForward.Magnitude() > 0.0001f) {
            vecForward.Normalise();

            CVector worldUp(0.0f, 0.0f, 1.0f);
            CVector vecRight = CrossProduct(worldUp, vecForward);
            if (vecRight.Magnitude() > 0.0001f) {
                vecRight.Normalise();

                CVector vecUp = CrossProduct(vecForward, vecRight);
                vecUp.Normalise();

                // Validação segura da CCam ativa antes de escrever
                uintptr_t camAddr = GameSA::TheCameraAddr();
                if (camAddr) {
                    CCam& activeCam = GameSA::GetActiveCam();
                    activeCam.m_vecSource = g_CurrentCamPos;
                    activeCam.m_vecFront = vecForward;
                    activeCam.m_vecUp = vecUp;
                    activeCam.m_vecTargetCoorsForFudgeInter = g_CurrentLookAt;
                    activeCam.m_fFOV = g_Config.fFOV;

                    if (pCamPlaceable->m_matrix) {
                        pCamPlaceable->m_matrix->right = vecRight;
                        pCamPlaceable->m_matrix->up = vecForward;
                        pCamPlaceable->m_matrix->at = vecUp;
                        pCamPlaceable->m_matrix->pos = g_CurrentCamPos;
                        pCamPlaceable->m_matrix->UpdateRW();
                    }
                    GameSA::SetRwCamPosition();
                    GameSA::CalculateDerivedValues(false, true);
                    GameSA::SetCameraOrientation(g_CurrentYawRad);
                }
            }
        }
    }
