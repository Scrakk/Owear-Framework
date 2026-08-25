// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// EventToken.h mínimo — WebView2.h lo incluye y el SDK de Windows a veces no
// lo trae en rutas de include de MinGW. Definición canónica de la WRL.
#pragma once

typedef struct EventRegistrationToken {
    long long value;
} EventRegistrationToken;
