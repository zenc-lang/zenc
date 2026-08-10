// SPDX-License-Identifier: MIT
#ifndef ZC_DIAG_H
#define ZC_DIAG_H

/**
 * @file zc_diag.h
 * @brief Public API for diagnostics: error/warning/panic reporting.
 */

#include "token.h"

/**
 * @brief Report a fatal error and exit (or abort if fault-tolerant).
 */
void zpanic_at(Token t, const char *fmt, ...);

/**
 * @brief Report a compilation error.
 */
void zerror_at(Token t, const char *fmt, ...);

/**
 * @brief Report a warning.
 */
void zwarn_at(Token t, const char *fmt, ...);

/**
 * @brief Report a warning with a suggestion.
 */
void zwarn_with_suggestion(Token t, const char *msg, const char *suggestion);

#endif // ZC_DIAG_H
