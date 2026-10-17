package com.learngraphicsprogramming.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.widget.Toast

/**
 * Informational only: never denies, defers or requests permission for a download.
 * In particular, a metered Wi-Fi connection is not incorrectly labeled as mobile data.
 */
internal object MobileDataDownloadNotice {
    fun showIfCellular(context: Context) {
        val connectivity = context.getSystemService(Context.CONNECTIVITY_SERVICE)
            as? ConnectivityManager ?: return
        val cellular = runCatching {
            val active = connectivity.activeNetwork ?: return@runCatching false
            connectivity.getNetworkCapabilities(active)
                ?.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) == true
        }.getOrDefault(false)
        if (!cellular) return

        val localized = UiLanguageSettings.localizedContext(context)
        Toast.makeText(context, localized.getString(R.string.download_using_mobile_data),
            Toast.LENGTH_LONG).show()
    }
}
