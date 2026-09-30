package com.learngraphicsprogramming.app

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ProgressBar
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import android.content.res.ColorStateList

class LessonAdapter(
    private val lessons: List<Lesson>,
    private val onLessonClick: (Lesson) -> Unit
) : RecyclerView.Adapter<LessonAdapter.LessonViewHolder>() {

    class LessonViewHolder(
        itemView: View
    ) : RecyclerView.ViewHolder(
        itemView
    ) {

        val textLessonNumber: TextView =
            itemView.findViewById(
                R.id.textLessonNumber
            )

        val textLessonTitle: TextView =
            itemView.findViewById(
                R.id.textLessonTitle
            )

        val textLessonProgress: TextView =
            itemView.findViewById(
                R.id.textLessonProgress
            )

        val textLessonPercentage: TextView =
            itemView.findViewById(
                R.id.textLessonPercentage
            )

        val progressLesson: ProgressBar =
            itemView.findViewById(
                R.id.progressLesson
            )
    }


    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): LessonViewHolder {

        val view =
            LayoutInflater
                .from(
                    parent.context
                )
                .inflate(
                    R.layout.item_lesson,
                    parent,
                    false
                )

        return LessonViewHolder(
            view
        )
    }


    override fun onBindViewHolder(
        holder: LessonViewHolder,
        position: Int
    ) {

        val lesson =
            lessons[position]

        val context =
            holder.itemView.context

        val accentColor =
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    context,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )

        val accentTextColor =
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    context,
                    AppearanceSettingsRepository.ROUTE_PROGRAMMING
                )

        holder.textLessonNumber.text =
            "Lección ${lesson.number}"

        holder.textLessonNumber
            .setTextColor(
                accentTextColor
            )

        holder.textLessonTitle.text =
            lesson.title

        val progress =
            ProgressRepository
                .getLessonPageProgress(
                    context,
                    lesson.id
                )


        holder.textLessonProgress.text =
            "${progress.completed} de " +
                    "${progress.total} páginas completadas"

        holder.textLessonPercentage.text =
            "${progress.percentage} %"

        holder.progressLesson.max =
            100

        holder.progressLesson.progress =
            progress.percentage

        holder.progressLesson.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        holder.itemView.setOnClickListener {

            onLessonClick(
                lesson
            )
        }
    }


    override fun getItemCount(): Int {

        return lessons.size
    }
}