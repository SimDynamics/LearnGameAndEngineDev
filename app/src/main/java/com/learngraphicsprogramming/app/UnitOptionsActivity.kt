package com.learngraphicsprogramming.app

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class UnitOptionsActivity : AppCompatActivity() {

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        setContentView(
            R.layout.activity_unit_options
        )

        val courseId =
            intent.getStringExtra(
                "courseId"
            ) ?: return

        val unitId =
            intent.getStringExtra(
                "unitId"
            ) ?: return

        val unitNumber =
            intent.getIntExtra(
                "unitNumber",
                1
            )

        val unitTitle =
            intent.getStringExtra(
                "unitTitle"
            ) ?: ""

        val buttonBack =
            findViewById<Button>(
                R.id.buttonBack
            )

        val textUnitNumber =
            findViewById<TextView>(
                R.id.textUnitNumber
            )

        val textUnitTitle =
            findViewById<TextView>(
                R.id.textUnitTitle
            )

        val buttonResetUnit =
            findViewById<Button>(
                R.id.buttonResetUnit
            )

        textUnitNumber.text =
            "Unidad $unitNumber"

        textUnitNumber.setTextColor(
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    this,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )
        )

        textUnitTitle.text =
            unitTitle

        buttonBack.setOnClickListener {

            finish()
        }

        buttonResetUnit.setOnClickListener {

            AlertDialog.Builder(
                this
            )
                .setTitle(
                    "Reiniciar unidad"
                )
                .setMessage(
                    "Se eliminarán el progreso y las respuestas " +
                            "de todas las lecciones de esta unidad.\n\n" +
                            "Esta acción no se puede deshacer."
                )
                .setNegativeButton(
                    "Cancelar",
                    null
                )
                .setPositiveButton(
                    "Reiniciar"
                ) { _, _ ->

                    ProgressRepository.resetUnit(
                        this,
                        courseId,
                        unitId
                    )

                    Toast.makeText(
                        this,
                        "Unidad reiniciada",
                        Toast.LENGTH_SHORT
                    ).show()

                    finish()
                }
                .show()
        }
    }

    override fun onResume() {

        super.onResume()

        AppearanceUi.applyRoute(
            this,
            AppearanceSettingsRepository.ROUTE_PROGRAMMING
        )
    }
}